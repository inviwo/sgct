/*
 * SGCTMpcdi.cpp
 *
 *  Created on: Jul 3, 2017
 *      Author: Gene Payne
 */

#include <sgct/SGCTMpcdi.h>

#include <sgct/ClusterManager.h>
#include <sgct/MessageHandler.h>
#include <sgct/Viewport.h>
#include <algorithm>
#include <sstream>
#include "unzip.h"
#include <zip.h>

namespace {
    bool doesStringHaveSuffix(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() &&
            str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool openZipFile(FILE* cfgFile, const std::string& cfgFilePath, unzFile* zipfile) {
#if (_MSC_VER >= 1400) //visual studio 2005 or later
        if (fopen_s(&cfgFile, cfgFilePath.c_str(), "r") != 0 || !cfgFile)
#else
        cfgFile = fopen(cfgFilePath.c_str(), "r");
        if (cfgFile == nullptr)
#endif
        {
            sgct::MessageHandler::instance()->print(
                sgct::MessageHandler::Level::Error,
                "parseMpcdiConfiguration: Failed to open file %s\n",
                cfgFilePath.c_str()
            );
            return false;
        }
        //Open MPCDI file (zip compressed format)
        *zipfile = unzOpen(cfgFilePath.c_str());
        if (zipfile == nullptr) {
            sgct::MessageHandler::instance()->print(
                sgct::MessageHandler::Level::Error,
                "parseMpcdiConfiguration: Failed to open compressed mpcdi file %s\n",
                cfgFilePath.c_str()
            );
            return false;
        }
        return true;
    }

    void unsupportedFeatureCheck(std::string tag, std::string featureName) {
        if (featureName == tag) {
            std::string warn = "ReadConfigMpcdi: Unsupported feature: " + featureName + " \n";
            sgct::MessageHandler::instance()->print(
                sgct::MessageHandler::Level::Warning,
                warn.c_str()
            );
        }
    }

    bool checkAttributeForExpectedValue(tinyxml2::XMLElement* elem,
                                        const std::string& attrRequired,
                                        const std::string& tagDescription,
                                        const std::string& expectedTag)
    {
        std::string errorMsg;
        const char* attr = elem->Attribute(attrRequired.c_str());
        if (attr != nullptr) {
            if (expectedTag != attr) {
                errorMsg = "parseMpcdiXml: Only " + tagDescription + " '" +
                    expectedTag + "' is supported.\n";
            }
        }
        else {
            errorMsg = "parseMpcdiXml: No " + tagDescription + " attribute found \n";
        }

        if (!errorMsg.empty()) {
            sgct::MessageHandler::instance()->print(
                sgct::MessageHandler::Level::Error,
                errorMsg.c_str()
            );
            return false;
        }
        else {
            return true;
        }
    }
} // namespace

namespace sgct_core {

MpcdiSubFiles::MpcdiSubFiles() {
    for (int i = 0; i < Mpcdi_nRequiredFiles; ++i) {
        hasFound[i] = false;
        buffer[i] = nullptr;
    }
    extension[MpcdiXml] = "xml";
    extension[MpcdiPfm] = "pfm";
}

MpcdiSubFiles::~MpcdiSubFiles() {
    for (int i = 0; i < Mpcdi_nRequiredFiles; ++i) {
        if (buffer[i] != nullptr)
            delete buffer[i];
    }
}


SGCTMpcdi::SGCTMpcdi(std::string parentErrorMessage)
    : mErrorMsg(std::move(parentErrorMessage))
{}

SGCTMpcdi::~SGCTMpcdi() {
    for (MpcdiWarp* ptr : mWarp) {
        delete ptr;
    }
    for (MpcdiRegion* ptr : mBufferRegions) {
        delete ptr;
    }
}

bool SGCTMpcdi::parseConfiguration(const std::string& filenameMpcdi,
                                   SGCTNode& tmpNode,
                                   sgct::SGCTWindow& tmpWin)
{
    FILE* cfgFile = nullptr;
    unzFile zipfile;
    const int MaxFilenameSize_bytes = 500;

    bool fileOpenSuccess = openZipFile(cfgFile, filenameMpcdi, &zipfile);
    if (!fileOpenSuccess) {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "parseMpcdiConfiguration: Unable to open zip archive file %s\n",
            filenameMpcdi.c_str()
        );
        return false;
    }
    // Get info about the zip file
    unz_global_info global_info;
    int globalInfoRet = unzGetGlobalInfo(zipfile, &global_info);
    if (globalInfoRet != UNZ_OK) {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "parseMpcdiConfiguration: Unable to get zip archive info from %s\n",
            filenameMpcdi.c_str()
        );
        unzClose(zipfile);
        return false;
    }

    //Search for required files inside mpcdi archive file
    for (unsigned int i = 0; i < global_info.number_entry; ++i) {
        unz_file_info file_info;
        char filename[MaxFilenameSize_bytes];
        int getCurrentFileInfo = unzGetCurrentFileInfo(
            zipfile,
            &file_info,
            filename,
            MaxFilenameSize_bytes,
            nullptr,
            0,
            nullptr,
            0
        );
        if (getCurrentFileInfo != UNZ_OK) {
            sgct::MessageHandler::instance()->print(
                sgct::MessageHandler::Level::Error,
                "parseMpcdiConfiguration: Unable to get info on compressed file #%d\n", i
            );
            unzClose(zipfile);
            return false;
        }

        bool isSubFileValid =  processSubFiles(filename, &zipfile, file_info);
        if (!isSubFileValid) {
            unzClose(zipfile);
            return false;
        }
        if ((i + 1) < global_info.number_entry) {
            int goToNextFileStatus = unzGoToNextFile(zipfile);
            if (goToNextFileStatus != UNZ_OK) {
                sgct::MessageHandler::instance()->print(
                    sgct::MessageHandler::Level::Warning,
                    "parseMpcdiConfiguration: Unable to get next file in archive\n"
                );
            }
        }
    }
    unzClose(zipfile);
    bool hasXmlFile = mMpcdiSubFileContents.hasFound[MpcdiSubFiles::MpcdiXml];
    bool hasPfmFile = mMpcdiSubFileContents.hasFound[MpcdiSubFiles::MpcdiPfm];
    if( !hasXmlFile || !hasPfmFile) {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "parseMpcdiConfiguration: mpcdi file %s does not contain xml and/or pfm file\n",
            filenameMpcdi.c_str()
        );
        return false;
    }
    if (!readAndParseXMLString(tmpNode, tmpWin)) {
        return false;
    }
    else {
        return true;
    }
}



bool SGCTMpcdi::processSubFiles(std::string filename, unzFile* zipfile,
                                unz_file_info& file_info)
{
    for (int i = 0; i < mMpcdiSubFileContents.Mpcdi_nRequiredFiles; ++i) {
        if( !mMpcdiSubFileContents.hasFound[i]
            && doesStringHaveSuffix(filename, mMpcdiSubFileContents.extension[i]))
        {
            mMpcdiSubFileContents.hasFound[i] = true;
            mMpcdiSubFileContents.size[i] = file_info.uncompressed_size;
            mMpcdiSubFileContents.filename[i] = filename;
            int openCurrentFile = unzOpenCurrentFile(*zipfile);
            if (openCurrentFile != UNZ_OK) {
                sgct::MessageHandler::instance()->print(
                    sgct::MessageHandler::Level::Error,
                    "parseMpcdiConfiguration: Unable to open %s\n", filename.c_str()
                );
                unzClose(*zipfile);
                return false;
            }
            mMpcdiSubFileContents.buffer[i] = new char[file_info.uncompressed_size];
            if( mMpcdiSubFileContents.buffer[i] != nullptr) {
                int error = unzReadCurrentFile(
                    *zipfile,
                    mMpcdiSubFileContents.buffer[i],
                    file_info.uncompressed_size
                );
                if (error < 0) {
                    sgct::MessageHandler::instance()->print(
                        sgct::MessageHandler::Level::Error,
                        "parseMpcdiConfiguration: %s read from %s failed.\n",
                        mMpcdiSubFileContents.extension[i].c_str(), filename.c_str()
                    );
                    unzClose(*zipfile);
                    return false;
                }
            }
            else {
                sgct::MessageHandler::instance()->print(
                    sgct::MessageHandler::Level::Error,
                    "parseMpcdiConfiguration: Unable to allocate memory for %s\n",
                    filename.c_str()
                );
                unzClose(*zipfile);
                return false;
            }
        }
    }
    return true;
}

bool SGCTMpcdi::readAndParseXMLString(SGCTNode& tmpNode, sgct::SGCTWindow& tmpWin) {
    bool mpcdiParseResult = false;
    if (mMpcdiSubFileContents.buffer[MpcdiSubFiles::MpcdiXml] != nullptr) {
        tinyxml2::XMLDocument xmlDoc;
        tinyxml2::XMLError result = xmlDoc.Parse(
            mMpcdiSubFileContents.buffer[MpcdiSubFiles::MpcdiXml],
            mMpcdiSubFileContents.size[MpcdiSubFiles::MpcdiXml]
        );

        if (result != tinyxml2::XML_NO_ERROR) {
            std::stringstream ss;
            if (xmlDoc.GetErrorStr1() && xmlDoc.GetErrorStr2()) {
                ss << "Parsing failed after: " << xmlDoc.GetErrorStr1() << " "
                   << xmlDoc.GetErrorStr2();
            }
            else if (xmlDoc.GetErrorStr1()) {
                ss << "Parsing failed after: " << xmlDoc.GetErrorStr1();
            }
            else if (xmlDoc.GetErrorStr2()) {
                ss << "Parsing failed after: " << xmlDoc.GetErrorStr2();
            }
            else {
                ss << "File not found";
            }
            mErrorMsg = ss.str();
            mpcdiParseResult = false;
        }
        else {
            mpcdiParseResult = readAndParseXML_mpcdi(xmlDoc, tmpNode, tmpWin);
        }
    }
    return mpcdiParseResult;
}

bool SGCTMpcdi::readAndParseXML_mpcdi(tinyxml2::XMLDocument& xmlDoc, SGCTNode& tmpNode,
                                      sgct::SGCTWindow& tmpWin)
{
    tinyxml2::XMLElement* XMLroot = xmlDoc.FirstChildElement("MPCDI");
    if (XMLroot == nullptr) {
        mErrorMsg = "Cannot find XML root!";
        return false;
    }
    constexpr const int MaxXmlDepth = 16;
    tinyxml2::XMLElement* element[MaxXmlDepth];
    for (unsigned int i = 0; i < MaxXmlDepth; i++) {
        element[i] = nullptr;
    }
    const char* val[MaxXmlDepth];

    bool hasExpectedValue;
    hasExpectedValue = checkAttributeForExpectedValue(
        XMLroot,
        "profile",
        "MPCDI profile",
        "3d"
    );
    if (!hasExpectedValue) {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "readAndParseXML_mpcdi: Problem with 'MPCDI' attribute in XML\n"
        );
        return false;
    }
    hasExpectedValue = checkAttributeForExpectedValue(
        XMLroot,
        "geometry",
        "MPCDI geometry level",
        "1"
    );
    if (!hasExpectedValue) {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "readAndParseXML_mpcdi: Problem with 'geometry' attribute in XML\n"
        );
        return false;
    }
    hasExpectedValue = checkAttributeForExpectedValue(
        XMLroot,
        "version",
        "MPCDI version",
        "2.0"
    );
    if (!hasExpectedValue) {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "readAndParseXML_mpcdi: Problem with 'version' attribute in XML\n"
        );
        return false;
    }

    MpcdiFoundItems parsedItems;
    element[0] = XMLroot->FirstChildElement();
    while (element[0] != nullptr) {
        val[0] = element[0]->Value();
        if (strcmp("display", val[0]) == 0) {
            if (!readAndParseXML_display(element, val, tmpNode, tmpWin, parsedItems)) {
                return false;
            }
        }
        else if (strcmp("files", val[0]) == 0) {
            if (!readAndParseXML_files(element, val, tmpWin)) {
                return false;
            }
        }
        unsupportedFeatureCheck(val[0], "extensionSet");
        //iterate
        element[0] = element[0]->NextSiblingElement();
    }

    return true;
}

bool SGCTMpcdi::readAndParseXML_display(tinyxml2::XMLElement* element[],
                                        const char* val[], SGCTNode& tmpNode,
                                        sgct::SGCTWindow& tmpWin,
                                        MpcdiFoundItems& parsedItems)
{
    if (parsedItems.haveDisplayElem) {
         sgct::MessageHandler::instance()->print(
             sgct::MessageHandler::Level::Error,
             "parseMpcdiXml: Multiple 'display' elements not supported.\n"
         );
         return false;
     }
     else {
         parsedItems.haveDisplayElem = true;
     }
     element[1] = element[0]->FirstChildElement();
     while (element[1] != nullptr) {
         val[1] = element[1]->Value();
         if (strcmp("buffer", val[1]) == 0) {
             if (!readAndParseXML_buffer(element, val, tmpWin, parsedItems)) {
                 return false;
             }
             tmpNode.addWindow(std::move(tmpWin));
         }
         //iterate
         element[1] = element[1]->NextSiblingElement();
     }
     return true;
}

bool sgct_core::SGCTMpcdi::readAndParseXML_files(tinyxml2::XMLElement* element[],
                                                 const char* val[],
                                                 sgct::SGCTWindow& tmpWin)
{
    std::string filesetRegionId;

    element[1] = element[0]->FirstChildElement();
    while (element[1] != nullptr) {
        val[1] = element[1]->Value();
        if (strcmp("fileset", val[1]) == 0) {
            if (element[1]->Attribute("region") != nullptr) {
                filesetRegionId = element[1]->Attribute("region");
            }
            element[2] = element[1]->FirstChildElement();
            while (element [2] != nullptr) {
                val[2] = element[2]->Value();
                if (strcmp("geometryWarpFile", val[2]) == 0) {
                    bool success = readAndParseXML_geoWarpFile(
                        element,
                        val,
                        tmpWin,
                        filesetRegionId
                    );
                    if (!success) {
                        return false;
                    }
                }
                unsupportedFeatureCheck(val[2], "alphaMap");
                unsupportedFeatureCheck(val[2], "betaMap");
                unsupportedFeatureCheck(val[2], "distortionMap");
                unsupportedFeatureCheck(val[2], "decodeLUT");
                unsupportedFeatureCheck(val[2], "correctLUT");
                unsupportedFeatureCheck(val[2], "encodeLUT");

                element[2] = element[2]->NextSiblingElement();
            }
        }
        element[1] = element[1]->NextSiblingElement();
    }
    return true;
}

bool SGCTMpcdi::readAndParseXML_geoWarpFile(tinyxml2::XMLElement* element[],
                                            const char* val[],
                                            sgct::SGCTWindow& tmpWin,
                                            std::string filesetRegionId)
{
    mWarp.push_back(new MpcdiWarp);
    mWarp.back()->id = filesetRegionId;
    element[3] = element[2]->FirstChildElement();
    while (element[3] != nullptr) {
        val[3] = element[3]->Value();
        if (strcmp("path", val[3]) == 0) {
            mWarp.back()->pathWarpFile = element[3]->GetText();
            mWarp.back()->haveFoundPath = true;
        }
        else if (strcmp("interpolation", val[3]) == 0) {
            std::string interpolation = element[3]->GetText();
            if (interpolation == "linear" != 0) {
                sgct::MessageHandler::instance()->print(
                    sgct::MessageHandler::Level::Warning,
                    "parseMpcdiXml: only linear interpolation is supported.\n"
                );
            }
            mWarp.back()->haveFoundInterpolation = true;
        }
        element[3] = element[3]->NextSiblingElement();
    }
    if (mWarp.back()->haveFoundPath && mWarp.back()->haveFoundInterpolation) {
        //Look for matching MPCDI region (SGCT viewport) to pass
        // the warp field data to
        bool foundMatchingPfmBuffer = false;
        for (int r = 0; r < tmpWin.getNumberOfViewports(); ++r) {
            std::string tmpWindowName = tmpWin.getViewport(r).getName();
            std::string currRegion_warpName = mWarp.back()->id;
            if (tmpWindowName == currRegion_warpName) {
                std::string currRegion_warpFilename = mWarp.back()->pathWarpFile;
                std::string matchingMpcdiDataFile
                    = mMpcdiSubFileContents.filename[MpcdiSubFiles::MpcdiPfm];
                if (currRegion_warpFilename == matchingMpcdiDataFile) {
                    std::vector<unsigned char> meshData;
                    meshData.assign(
                        mMpcdiSubFileContents.buffer[MpcdiSubFiles::MpcdiPfm],
                        mMpcdiSubFileContents.buffer[MpcdiSubFiles::MpcdiPfm] +
                            mMpcdiSubFileContents.size[MpcdiSubFiles::MpcdiPfm]
                    );
                    tmpWin.getViewport(r).setMpcdiWarpMesh(std::move(meshData));
                    foundMatchingPfmBuffer = true;
                }
            }
        }
        if (!foundMatchingPfmBuffer) {
            sgct::MessageHandler::instance()->print(
                sgct::MessageHandler::Level::Error,
                "parseMpcdiXml: matching geometryWarpFile not found.\n"
            );
            return false;
        }
    }
    else {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "parseMpcdiXml: geometryWarpFile requires both path and interpolation.\n"
        );
        return false;
    }
    return true;
}


bool SGCTMpcdi::readAndParseXML_buffer(tinyxml2::XMLElement* element[],
                                       const char* val[], sgct::SGCTWindow& tmpWin,
                                       MpcdiFoundItems& parsedItems)
{
    if (parsedItems.haveBufferElem) {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "parseMpcdiXml: Multiple 'buffer' elements unsupported.\n"
        );
        return false;
    }
    else {
        parsedItems.haveBufferElem = true;
    }
    if (element[1]->Attribute("xResolution") != nullptr) {
        element[1]->QueryAttribute("xResolution", &parsedItems.resolutionX);
    }
    if (element[1]->Attribute("yResolution") != nullptr) {
        element[1]->QueryAttribute("yResolution", &parsedItems.resolutionY);
    }
    if (parsedItems.resolutionX >= 0 && parsedItems.resolutionY >= 0) {
        tmpWin.initWindowResolution(glm::ivec2(parsedItems.resolutionX, parsedItems.resolutionY));
        tmpWin.setFramebufferResolution(glm::ivec2(parsedItems.resolutionX, parsedItems.resolutionY));
        tmpWin.setFixResolution(true);
    }
    else {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "parseMpcdiXml: Require both xResolution and yResolution values.\n"
        );
        return false;
    }
    //Assume a 0,0 offset for an MPCDI buffer, which maps to an SGCT window
    tmpWin.setWindowPosition(glm::ivec2(0, 0));

    element[2] = element[1]->FirstChildElement();
    while (element[2] != nullptr) {
        val[2] = element[2]->Value();
        if (strcmp("region", val[2]) == 0) {
            if (!readAndParseXML_region(element, val, tmpWin, parsedItems)) {
                return false;
            }
        }
        unsupportedFeatureCheck(val[2], "coordinateFrame");
        unsupportedFeatureCheck(val[2], "color");
        //iterate
        element[2] = element[2]->NextSiblingElement();
    }
    return true;
}

bool SGCTMpcdi::readAndParseXML_region(tinyxml2::XMLElement* element[],
                                       const char* val[], sgct::SGCTWindow& tmpWin,
                                       MpcdiFoundItems& parsedItems)
{
    //Require an 'id' attribute for each region. These will be compared later to the
    // fileset, in which there must be a matching 'id'. The mBufferRegions vector is
    // intended for use with MPCDI files containing multiple regions, but currently
    // only is tested with single region files.
    if (element[2]->Attribute("id") != nullptr) {
        mBufferRegions.push_back(new MpcdiRegion);
        mBufferRegions.back()->id = element[2]->Attribute("id");
    }
    else {
        sgct::MessageHandler::instance()->print(
            sgct::MessageHandler::Level::Error,
            "parseMpcdiXml: No 'id' attribute provided for region.\n"
        );
        return false;
    }
    std::unique_ptr<Viewport> vp = std::make_unique<Viewport>();
    vp->configureMpcdi(element, val, parsedItems.resolutionX, parsedItems.resolutionY);
    tmpWin.addViewport(std::move(vp));
    return true;
}

} // namespace sgct_core
