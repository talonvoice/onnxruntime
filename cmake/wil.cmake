# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
add_library(wil INTERFACE)

 if(onnxruntime_USE_SUBMODULE)
       target_include_directories(wil INTERFACE external/wil/include/)
    else()
    FetchContent_Declare(
      wil
      GIT_REPOSITORY https://github.com/microsoft/wil.git
      GIT_TAG        e8c599bca6c56c44b6730ad93f6abbc9ecd60fc1
    )
    FetchContent_Populate(wil)
    target_include_directories(wil INTERFACE ${wil_SOURCE_DIR}/include)
endif()
