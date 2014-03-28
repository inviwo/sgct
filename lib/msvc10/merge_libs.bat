if exist sgct_light.lib (
	if exist sgct_lightd.lib (
		goto CREATE_ALL
	)
)

if exist sgct_light.lib goto CREATE_RELEASE
if exist sgct_lightd.lib goto CREATE_DEBUG

:CREATE_ALL
DEL sgct.lib
DEL sgctd.lib
call "%VS100COMNTOOLS%"\vsvars32.bat
lib /LTCG /OUT:sgct.lib sgct_light.lib deps\freetype.lib deps\glew.lib deps\glfw3.lib deps\libpng15_static.lib deps\turbojpeg-static.lib deps\tinythreadpp.lib deps\tinyxml2.lib deps\vrpn.lib deps\zlibstatic.lib
lib /LTCG /OUT:sgctd.lib sgct_lightd.lib deps\freetyped.lib deps\glewd.lib deps\glfw3d.lib deps\libpng15_staticd.lib deps\turbojpeg-staticd.lib deps\tinythreadppd.lib deps\tinyxml2d.lib deps\vrpnd.lib deps\zlibstaticd.lib
goto END

:CREATE_DEBUG
DEL sgctd.lib
call "%VS100COMNTOOLS%"\vsvars32.bat
lib /LTCG /OUT:sgctd.lib sgct_lightd.lib deps\freetyped.lib deps\glewd.lib deps\glfw3d.lib deps\libpng15_staticd.lib deps\turbojpeg-staticd.lib deps\tinythreadppd.lib deps\tinyxml2d.lib deps\vrpnd.lib deps\zlibstaticd.lib
goto END

:CREATE_RELEASE
DEL sgct.lib
call "%VS100COMNTOOLS%"\vsvars32.bat
lib /LTCG /OUT:sgct.lib sgct_light.lib deps\freetype.lib deps\glew.lib deps\glfw3.lib deps\libpng15_static.lib deps\turbojpeg-static.lib deps\tinythreadpp.lib deps\tinyxml2.lib deps\vrpn.lib deps\zlibstatic.lib
goto END

:END