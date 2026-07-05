{
    "targets": [
        {
            "target_name": "aalink",
            "sources": ["src/aalink.cpp"],
            "include_dirs": [
                "<!(node -p \"require('node-addon-api').include_dir\")",
                "ext/link/include",
                "ext/link/modules/asio-standalone/asio/include"
            ],
            "defines": ["NAPI_VERSION=8", "NAPI_CPP_EXCEPTIONS"],
            "cflags_cc": ["-std=c++17", "-fexceptions"],
            "cflags_cc!": ["-fno-exceptions", "-fno-rtti"],
            "conditions": [
                ["OS=='linux'", {
                    "defines": ["LINK_PLATFORM_LINUX=1"]
                }],
                ["OS=='mac'", {
                    "defines": ["LINK_PLATFORM_MACOSX=1"],
                    "xcode_settings": {
                        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
                        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
                        "MACOSX_DEPLOYMENT_TARGET": "10.15"
                    }
                }],
                ["OS=='win'", {
                    "defines": [
                        "LINK_PLATFORM_WINDOWS=1",
                        "_WIN32_WINNT=0x0601",
                        "WIN32_LEAN_AND_MEAN",
                        "NOMINMAX"
                    ],
                    "libraries": ["ws2_32.lib", "iphlpapi.lib"],
                    "msvs_settings": {
                        "VCCLCompilerTool": {
                            "ExceptionHandling": 1,
                            "AdditionalOptions": ["/std:c++17"]
                        }
                    }
                }]
            ]
        }
    ]
}
