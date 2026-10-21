# Fails if a source of terrain_core calls a floating-point function whose result depends on the
# platform: the transcendental functions of the C and C++ libraries (qualified or not, float and
# long double variants, compiler builtins), fused multiply-adds, std::lerp (each standard library
# implements it its own way) or long double (64, 80 or 128 bits depending on the platform).
# terrain::dmath (det_math.hpp) provides deterministic replacements; std::sqrt is allowed, as
# IEEE 754 rounds it correctly. Comments are ignored.
#
# Before scanning, the script checks itself on generated files: one line per forbidden form must
# be caught, and allowed forms must not be.
#
# cmake -DSOURCE_DIR=<terrain_core> -DWORK_DIR=<scratch directory> -P forbidden_math_test.cmake
cmake_minimum_required(VERSION 3.20)

set(names "sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|exp|exp2|expm1")
string(APPEND names "|log|log2|log10|log1p|logb|pow|hypot|cbrt|erf|erfc|tgamma|lgamma|fma")
set(before "(^|[^A-Za-z0-9_.>:])")
set(open "[ \t]*\\(")
# Anywhere: a call, qualified or not, with a suffix f or l, or a builtin.
set(any_call "${before}(std::|::)?(__builtin_)?(${names})(f|l)?${open}")
# det_math defines and calls the unqualified names: there, only qualified, builtin or suffixed
# calls are forbidden.
set(foreign_call "${before}((std::|::|__builtin_)(${names})(f|l)?|(${names})(f|l))${open}")
set(always "${before}fma(f|l)?${open}|std::lerp${open}|long[ \t]+double")

# Lines of file that hold a forbidden form, as "file:line: text" entries appended to out.
function(scan file out)
    get_filename_component(stem "${file}" NAME_WE)
    if(stem STREQUAL "det_math")
        set(pattern "${foreign_call}|${always}")
    else()
        set(pattern "${any_call}|${always}")
    endif()
    file(READ "${file}" text)
    # Keep the lines whole when splitting them into a CMake list.
    string(REPLACE ";" "<sc>" text "${text}")
    string(REPLACE "[" "<lb>" text "${text}")
    string(REPLACE "]" "<rb>" text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(found "${${out}}")
    set(number 0)
    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")
        string(REGEX REPLACE "//.*$" "" code "${line}")
        string(REGEX REPLACE "dmath::[A-Za-z0-9_]+" "" code "${code}")
        if(code MATCHES "${pattern}")
            list(APPEND found "${file}:${number}: ${line}")
        endif()
    endforeach()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

# Self-check on generated files.
set(trap_lines
    "double a = std::pow(x, 2.0)<sc>"
    "double b = pow(x, 2.0)<sc>"
    "double c = ::cos(x)<sc>"
    "float d = powf(x, 2.0f)<sc>"
    "float e = sinf(x)<sc>"
    "double f = std::hypot(x, y)<sc>"
    "double g = atan2(y, x)<sc>"
    "double h = std::exp (x)<sc>"
    "double i = __builtin_sin(x)<sc>"
    "double j = std::fma(x, y, z)<sc>"
    "double k = fma(x, y, z)<sc>"
    "double l = std::lerp(x, y, t)<sc>"
    "long double m = 0.0<sc>"
    "double n = cbrt(x) + tanh(y)<sc>"
    "double o = std::log2(x)<sc>"
    "auto p = expl(x)<sc>")
set(clean_lines
    "double a = dmath::pow(x, 2.0) + dmath::sinTurns(t)<sc>"
    "double b = lerp(x, y, t) + smoothstep(0.0f, 1.0f, x) + std::sqrt(x)<sc>"
    "double c = grid.pow(2) + other::cos(x) + node->log(x)<sc>"
    "// std::sin(x) in a comment, and tanh(macro / width)"
    "double d = cosine(x) + powerOf(x) + exponent<sc> // explain(x)")
set(trap_file "${WORK_DIR}/forbidden_math_trap.cpp")
set(clean_file "${WORK_DIR}/forbidden_math_clean.cpp")
string(REPLACE ";" "\n" trap_text "${trap_lines}")
string(REPLACE ";" "\n" clean_text "${clean_lines}")
string(REPLACE "<sc>" ";" trap_text "${trap_text}")
string(REPLACE "<sc>" ";" clean_text "${clean_text}")
file(WRITE "${trap_file}" "${trap_text}\n")
file(WRITE "${clean_file}" "${clean_text}\n")
set(trap_found "")
scan("${trap_file}" trap_found)
list(LENGTH trap_found trap_count)
list(LENGTH trap_lines trap_expected)
set(clean_found "")
scan("${clean_file}" clean_found)
if(NOT trap_count EQUAL trap_expected OR clean_found)
    message(FATAL_ERROR "forbidden_math_test checks itself badly: ${trap_count} of "
                        "${trap_expected} trap lines caught, false hits: ${clean_found}")
endif()

file(GLOB_RECURSE sources "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.hpp"
     "${SOURCE_DIR}/include/*.hpp" "${SOURCE_DIR}/tools/*.cpp")
set(found "")
foreach(source IN LISTS sources)
    scan("${source}" found)
endforeach()
list(LENGTH sources count)
if(found)
    string(REPLACE ";" "\n" report "${found}")
    string(REPLACE "<sc>" ";" report "${report}")
    string(REPLACE "<lb>" "[" report "${report}")
    string(REPLACE "<rb>" "]" report "${report}")
    message(FATAL_ERROR "non-deterministic floating-point functions in terrain_core "
                        "(use terrain::dmath):\n${report}")
endif()
message(STATUS "forbidden_math_test: ${count} sources of terrain_core checked, none found")
