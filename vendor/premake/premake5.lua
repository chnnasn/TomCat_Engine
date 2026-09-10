project "Premake"
	kind "Utility"

	targetdir ("%{wks.location}/bin/" .. outputdir .."/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .."/%{prj.name}")

	files
	{
		"%{wks.location}/**premake5.lua"
	}

	postbuildmessage "Regenerating project files with Premake5!"
	postbuildcommands
	{
		"if exist \"%{prj.location}bin\\premake5.exe\" (\"%{prj.location}bin\\premake5.exe\" %{_ACTION} --file=\"%{wks.location}premake5.lua\") else (echo Premake5 executable not found - skipping project regeneration.)"
	}
