<div align="center">

<img src="docs/logo.png" alt="tiny-page logo" />

# tiny-page

**A tiny, pixel-perfect 2D game engine and editor written in C++.**

![C++20](https://img.shields.io/badge/C%2B%2B-20-42A5F5?style=flat-square&logo=cplusplus&logoColor=white)
![SDL3](https://img.shields.io/badge/SDL-3-42A5F5?style=flat-square)
![Dear ImGui](https://img.shields.io/badge/Dear%20ImGui-editor-42A5F5?style=flat-square)
![CMake](https://img.shields.io/badge/CMake-3.24+-42A5F5?style=flat-square&logo=cmake&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Windows-42A5F5?style=flat-square&logo=windows&logoColor=white)
![License](https://img.shields.io/badge/license-MIT-42A5F5?style=flat-square)

</div>

<br />

## About

tiny-page is a small 2D game engine with an editor, written in C++20. It aims to stay simple and
readable: few dependencies, small headers, and one shared look across every window.

> [!NOTE]
> tiny-page is in early development. Things will change.

## Built with

- [SDL3](https://libsdl.org/) for windows, rendering and input
- [Dear ImGui](https://github.com/ocornut/imgui) for the editor panels
- [CMake](https://cmake.org/) for building

## Requirements

- Windows 10 or 11
- [CMake](https://cmake.org/) 3.24 or newer
- A MinGW-w64 `g++` with C++20 support
- [Git](https://git-scm.com/) (CMake uses it to download SDL3 and Dear ImGui)

## Getting started

Clone the repository:

```powershell
git clone https://github.com/Spolii/tiny-page.git
cd tiny-page
```

Build it:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Run it:

```powershell
.\build\tiny-page.exe
```

The first configure downloads SDL3 and Dear ImGui, so it takes a little longer.

## License

Released under the [MIT License](LICENSE).