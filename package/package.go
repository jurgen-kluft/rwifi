package rwifi

import (
	ccore "github.com/jurgen-kluft/ccore/package"
	csdk "github.com/jurgen-kluft/csdk/package"
	cunittest "github.com/jurgen-kluft/cunittest/package"
	denv "github.com/jurgen-kluft/go-ide/denv"
	rcore "github.com/jurgen-kluft/rcore/package"
	rhome "github.com/jurgen-kluft/rhome/package"
)

// rwifi is the WiFi package for Arduino Esp32/Esp8266 projects.
const (
	repo_path = "github.com\\jurgen-kluft"
	repo_name = "rwifi"
)

func GetPackage() *denv.Package {
	name := repo_name

	// dependencies
	rcorepkg := rcore.GetPackage()
	rhomepkg := rhome.GetPackage()
	csdkpkg := csdk.GetPackage()
	ccorepkg := ccore.GetPackage()
	cunittestpkg := cunittest.GetPackage()

	// main package
	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(ccorepkg)
	mainpkg.AddPackage(rcorepkg)
	mainpkg.AddPackage(rhomepkg)
	mainpkg.AddPackage(csdkpkg)
	mainpkg.AddPackage(cunittestpkg)

	// esp32 library
	esp32wifilib := denv.SetupCppLibProjectForArduinoEsp32(mainpkg, name+"-esp32")
	esp32wifilib.AddDependencies(rcorepkg.GetMainLib())
	esp32wifilib.AddExternalInclude("{ESP32_SDK}", "libraries/WiFi", "src")
	esp32wifilib.AddExternalInclude("{ESP32_SDK}", "libraries/Network", "src")
	esp32wifilib.AddSourceFilesFrom("{ESP32_SDK}", "libraries/WiFi", "src", ".cpp")
	esp32wifilib.AddSourceFilesFrom("{ESP32_SDK}", "libraries/Network", "src", ".cpp")

	// esp8266 library
	esp8266wifilib := denv.SetupCppLibProjectForArduinoEsp8266(mainpkg, name+"-esp8266")
	esp8266wifilib.AddDependencies(rcorepkg.GetMainLib())
	esp8266wifilib.AddExternalInclude("{ESP8266_SDK}", "libraries/ESP8266WiFi", "src")
	esp8266wifilib.AddSourceFilesFrom("{ESP8266_SDK}", "libraries/ESP8266WiFi", "src", ".cpp")

	// main library
	mainlib := denv.SetupCppLibProject(mainpkg, name)
	mainlib.AddDependencies(rcorepkg.GetMainLib())
	mainlib.AddDependencies(rhomepkg.GetMainLib())
	mainlib.AddDependency(esp32wifilib)
	mainlib.AddDependency(esp8266wifilib)

	// test library
	testlib := denv.SetupCppTestLibProject(mainpkg, name)
	testlib.AddDependencies(ccorepkg.GetTestLib())
	testlib.AddDependencies(rcorepkg.GetTestLib())
	testlib.AddDependencies(rhomepkg.GetTestLib())

	// unittest project
	maintest := denv.SetupCppTestProject(mainpkg, name)
	maintest.AddDependencies(cunittestpkg.GetTestLib())
	maintest.AddDependency(testlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddMainLib(esp32wifilib)
	mainpkg.AddMainLib(esp8266wifilib)
	mainpkg.AddTestLib(testlib)
	mainpkg.AddUnittest(maintest)
	return mainpkg
}
