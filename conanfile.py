from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout

class OmniTraderConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": False}
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("boost/1.86.0", override=True)
        self.requires("fmt/12.1.0")
        self.requires("quill/11.0.2")
        self.requires("nlohmann_json/3.12.0")
        self.requires("glaze/6.2.0")
        self.requires("libcurl/8.17.0")
        self.requires("websocketpp/0.8.2")
        self.requires("concurrentqueue/1.0.4")
        self.requires("openssl/3.3.2")
        self.requires("argparse/3.2")

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
