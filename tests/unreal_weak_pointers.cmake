cmake_minimum_required(VERSION 3.20)

# The weak pointers of the plugin (agent 40, after the build of 3cdd7e1 in Unreal 5.8): a header
# that declares a TWeakObjectPtr must see the whole of FWeakObjectPtr, or the members of the
# template fail to compile (C2079) in the files that include it first. In 5.8,
# UObject/WeakObjectPtrTemplates.h only declares FWeakObjectPtr: such a header includes
# "UObject/WeakObjectPtr.h" itself, unless it includes "GameFramework/Actor.h" or
# "Components/ActorComponent.h", which bring it. The plugin is only ever built by Unreal: this
# reads its sources.
# Usage: cmake -DROOT=<repository> -P unreal_weak_pointers.cmake

set(plugin "${ROOT}/terrain_unreal/Source/TerrainEngine")
file(GLOB_RECURSE headers RELATIVE "${plugin}" "${plugin}/*.h")
list(SORT headers)

set(errors "")
set(checked 0)
foreach(header IN LISTS headers)
    file(READ "${plugin}/${header}" text)
    string(FIND "${text}" "TWeakObjectPtr<" weak)
    if(weak EQUAL -1)
        continue()
    endif()
    math(EXPR checked "${checked} + 1")
    string(FIND "${text}" "#include \"UObject/WeakObjectPtr.h\"" direct)
    string(FIND "${text}" "#include \"GameFramework/Actor.h\"" actor)
    string(FIND "${text}" "#include \"Components/ActorComponent.h\"" component)
    if(direct EQUAL -1 AND actor EQUAL -1 AND component EQUAL -1)
        string(APPEND errors "${header}: declares a TWeakObjectPtr without including "
                             "\"UObject/WeakObjectPtr.h\" (nor GameFramework/Actor.h or "
                             "Components/ActorComponent.h, which bring it; in 5.8 "
                             "WeakObjectPtrTemplates.h only declares FWeakObjectPtr)\n")
    endif()
endforeach()

if(NOT errors STREQUAL "")
    message(FATAL_ERROR "${errors}")
endif()
message(STATUS "${checked} headers declare a TWeakObjectPtr, each sees all of FWeakObjectPtr")
