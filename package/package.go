package rdno_wifi

import (
	denv "github.com/jurgen-kluft/ccode/denv"
	rdno_core "github.com/jurgen-kluft/rdno_core/package"
)

// rdno_wifi is the WiFi package for Arduino Esp32/Esp8266 projects.
const (
	repo_path = "github.com\\jurgen-kluft"
	repo_name = "rdno_wifi"
)

func GetPackage() *denv.Package {
	name := repo_name

	// dependencies
	corepkg := rdno_core.GetPackage()

	// main package
	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(corepkg)

	// esp32 library
	esp32wifilib := denv.SetupCppLibProjectForArduinoEsp32(mainpkg, name+"-esp32")
	esp32wifilib.AddDependencies(corepkg.GetMainLib())
	esp32wifilib.AddEnvironmentVariable("ESP32_SDK")
	esp32wifilib.AddExternalInclude("{ESP32_SDK}", "libraries/WiFi", "src")
	esp32wifilib.AddExternalInclude("{ESP32_SDK}", "libraries/Network", "src")
	esp32wifilib.AddSourceFilesFrom("{ESP32_SDK}", "libraries/WiFi", "src", ".cpp")
	esp32wifilib.AddSourceFilesFrom("{ESP32_SDK}", "libraries/Network", "src", ".cpp")

	// esp8266 library
	esp8266wifilib := denv.SetupCppLibProjectForArduinoEsp8266(mainpkg, name+"-esp8266")
	esp8266wifilib.AddDependencies(corepkg.GetMainLib())
	esp8266wifilib.AddEnvironmentVariable("ESP8266_SDK")
	esp8266wifilib.AddExternalInclude("{ESP8266_SDK}", "libraries/ESP8266WiFi", "src")
	esp8266wifilib.AddSourceFilesFrom("{ESP8266_SDK}", "libraries/ESP8266WiFi", "src", ".cpp")

	// main library
	mainlib := denv.SetupCppLibProject(mainpkg, name)
	mainlib.AddDependencies(corepkg.GetMainLib())
	mainlib.AddDependency(esp32wifilib)
	mainlib.AddDependency(esp8266wifilib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddMainLib(esp32wifilib)
	mainpkg.AddMainLib(esp8266wifilib)
	return mainpkg
}
