/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2024                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#ifndef __SGCT__READCONFIG__H__
#define __SGCT__READCONFIG__H__

#include <sgct/sgctexports.h>
#include <sgct/config.h>
#include <filesystem>
#include <string>

namespace sgct {

[[nodiscard]] SGCT_EXPORT config::Cluster readConfig(
    const std::filesystem::path& filename, const std::string& additionalError = "");

[[nodiscard]] SGCT_EXPORT config::Cluster readJsonConfig(std::string_view configuration);

[[nodiscard]] SGCT_EXPORT config::GeneratorVersion readJsonGeneratorVersion(
    const std::filesystem::path& configuration);

[[nodiscard]] SGCT_EXPORT config::GeneratorVersion readConfigGenerator(
    const std::filesystem::path& filename);

[[nodiscard]] SGCT_EXPORT std::string serializeConfig(const config::Cluster& cluster,
    std::optional<config::GeneratorVersion> genVersion = std::nullopt);

SGCT_EXPORT bool loadFileAndSchemaThenValidate(const std::filesystem::path& config,
    const std::filesystem::path& schema, const std::string& validationTypeExplanation);

SGCT_EXPORT bool validateConfigAgainstSchema(const std::string& stringifiedConfig,
    const std::string& stringifiedSchema, const std::filesystem::path& schemaDir);

// Putting noreturn after SGCT_EXPORT fails to build on msvc with dynamic linking.
[[noreturn]] SGCT_EXPORT void convertToSgctExceptionAndThrow(
    const std::filesystem::path& schema, const std::string& validationTypeExplanation,
    const std::string& exceptionMessage);

/**
 * Reads and returns meta information in the configuration file. Since meta is
 * non-critical, and is composed of only strings (blank by default), a Meta object is
 * always returned even if the source file does not contain any meta information.
 */
[[nodiscard]] SGCT_EXPORT sgct::config::Meta readMeta(
    const std::filesystem::path& filename);

} // namespace sgct

#endif // __SGCT__READCONFIG__H__
