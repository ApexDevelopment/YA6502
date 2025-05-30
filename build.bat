@echo off

if "%~1" == "clean" (
	echo Cleaning build directory...
	rmdir /s /q ".\build"
) else if "%~1" == "run" (
	echo Running the executable...
	".\build\main.exe"
) else (
	if not exist ".\build" (
		echo Creating build directory...
		mkdir ".\build"
	)
	echo Running cmake...
	cmake -S . -B .\build -G "MinGW Makefiles"
	if errorlevel 1 exit /b 1

	cd build
	mingw32-make
	if errorlevel 1 exit /b 1
	echo Done!
	cd ..
)