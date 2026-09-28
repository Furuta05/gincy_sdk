# Gincy release metadata synchronizer.
#
#   cmake -P tools/release.cmake                      regenerate every derived file from version.json
#   cmake -DVERSION=3.1.1 -P tools/release.cmake      change the product version, then regenerate
#   cmake -DCHECK=ON -P tools/release.cmake           fail when a derived file drifted from version.json
#
# Optional overrides written back to version.json: FRAMEWORK_API, NATIVE_ABI, PACKAGE_FORMAT,
# NETWORK_PROTOCOL, MANAGEMENT_API, CONTENT_SCHEMA_API, CHANNEL.
cmake_minimum_required(VERSION 3.20)
get_filename_component(LIST_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
if(EXISTS "${LIST_DIR}/../garrysmod/gamemodes/gincy")
    get_filename_component(ROOT "${LIST_DIR}/.." ABSOLUTE)
elseif(EXISTS "${LIST_DIR}/../../garrysmod/gamemodes/gincy")
    get_filename_component(ROOT "${LIST_DIR}/../.." ABSOLUTE)
else()
    get_filename_component(ROOT "${LIST_DIR}/.." ABSOLUTE)
endif()
if(EXISTS "${ROOT}/source/version.json" AND NOT EXISTS "${ROOT}/version.json")
    set(MANIFEST "${ROOT}/source/version.json")
else()
    set(MANIFEST "${ROOT}/version.json")
endif()
file(READ "${MANIFEST}" json)

function(semver_check name value)
    if(NOT value MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
        message(FATAL_ERROR "${name} must be a MAJOR.MINOR.PATCH version, got '${value}'")
    endif()
endfunction()
function(integer_check name value)
    if(NOT value MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${name} must be a positive integer, got '${value}'")
    endif()
endfunction()

set(changed OFF)
foreach(pair VERSION:product FRAMEWORK_API:framework_api NATIVE_ABI:native_abi PACKAGE_FORMAT:package_format
        NETWORK_PROTOCOL:network_protocol MANAGEMENT_API:management_api CONTENT_SCHEMA_API:content_schema_api CHANNEL:channel)
    string(REPLACE ":" ";" pair "${pair}")
    list(GET pair 0 variable)
    list(GET pair 1 key)
    if(DEFINED ${variable})
        if(key STREQUAL "product" OR key STREQUAL "framework_api")
            semver_check(${key} "${${variable}}")
            string(JSON json SET "${json}" ${key} "\"${${variable}}\"")
        elseif(key STREQUAL "channel")
            string(JSON json SET "${json}" ${key} "\"${${variable}}\"")
        else()
            integer_check(${key} "${${variable}}")
            string(JSON json SET "${json}" ${key} "${${variable}}")
        endif()
        set(changed ON)
    endif()
endforeach()

string(JSON PRODUCT GET "${json}" product)
string(JSON FRAMEWORK_API GET "${json}" framework_api)
string(JSON NATIVE_ABI GET "${json}" native_abi)
string(JSON PACKAGE_FORMAT GET "${json}" package_format)
string(JSON NETWORK_PROTOCOL GET "${json}" network_protocol)
string(JSON MANAGEMENT_API GET "${json}" management_api)
string(JSON CONTENT_SCHEMA_API GET "${json}" content_schema_api)
string(JSON CHANNEL GET "${json}" channel)
semver_check(product "${PRODUCT}")
semver_check(framework_api "${FRAMEWORK_API}")
foreach(key NATIVE_ABI PACKAGE_FORMAT NETWORK_PROTOCOL MANAGEMENT_API CONTENT_SCHEMA_API)
    integer_check(${key} "${${key}}")
endforeach()

string(REGEX MATCHALL "[0-9]+" product_parts "${PRODUCT}")
list(GET product_parts 0 PRODUCT_MAJOR)
list(GET product_parts 1 PRODUCT_MINOR)
math(EXPR PRODUCT_NEXT_MAJOR "${PRODUCT_MAJOR} + 1")
string(REGEX MATCHALL "[0-9]+" api_parts "${FRAMEWORK_API}")
list(GET api_parts 0 API_MAJOR)
list(GET api_parts 1 API_MINOR)
math(EXPR API_NEXT_MAJOR "${API_MAJOR} + 1")
set(REQUIRES_GINCY ">=${PRODUCT_MAJOR}.${PRODUCT_MINOR}.0 <${PRODUCT_NEXT_MAJOR}.0.0")
set(REQUIRES_API ">=${API_MAJOR}.${API_MINOR}.0 <${API_NEXT_MAJOR}.0.0")

set(drift "")
# Writes content when it differs; in CHECK mode records the drift instead.
function(emit path content)
    set(current "")
    if(EXISTS "${path}")
        file(READ "${path}" current)
    endif()
    if(NOT current STREQUAL content)
        if(CHECK)
            file(RELATIVE_PATH rel "${ROOT}" "${path}")
            set(drift "${drift}\n  ${rel}" PARENT_SCOPE)
        else()
            file(WRITE "${path}" "${content}")
            file(RELATIVE_PATH rel "${ROOT}" "${path}")
            message(STATUS "updated ${rel}")
        endif()
    endif()
endfunction()

if(changed AND NOT CHECK)
    file(WRITE "${MANIFEST}" "{
  \"product\": \"${PRODUCT}\",
  \"framework_api\": \"${FRAMEWORK_API}\",
  \"native_abi\": ${NATIVE_ABI},
  \"package_format\": ${PACKAGE_FORMAT},
  \"network_protocol\": ${NETWORK_PROTOCOL},
  \"management_api\": ${MANAGEMENT_API},
  \"content_schema_api\": ${CONTENT_SCHEMA_API},
  \"channel\": \"${CHANNEL}\"
}
")
    message(STATUS "updated version.json")
endif()

emit("${ROOT}/garrysmod/gamemodes/gincy/gamemode/core/sh_version.lua" "-- Generated from version.json by tools/release.cmake. Do not edit.
Gincy = Gincy or {}
Gincy.Version = {
    Product = \"${PRODUCT}\",
    FrameworkAPI = \"${FRAMEWORK_API}\",
    NativeABI = ${NATIVE_ABI},
    PackageFormat = ${PACKAGE_FORMAT},
    NetworkProtocol = ${NETWORK_PROTOCOL},
    ManagementAPI = ${MANAGEMENT_API},
    ContentSchemaAPI = ${CONTENT_SCHEMA_API},
    Channel = \"${CHANNEL}\",
    FirstPartyRequires = {gincy = \"${REQUIRES_GINCY}\", api = \"${REQUIRES_API}\"}
}
")

emit("${ROOT}/garrysmod/gincy_web/ui-version.json" "{\"product\":\"${PRODUCT}\",\"management_api\":${MANAGEMENT_API}}
")
if(EXISTS "${ROOT}/sdk/gincy_sdk/web/app.js")
    file(COPY "${ROOT}/sdk/gincy_sdk/web/app.js" DESTINATION "${ROOT}/garrysmod/gincy_web")
    file(COPY "${ROOT}/sdk/gincy_sdk/web/style.css" DESTINATION "${ROOT}/garrysmod/gincy_web")
    file(COPY "${ROOT}/sdk/gincy_sdk/web/index.html" DESTINATION "${ROOT}/garrysmod/gincy_web")
endif()

file(GLOB first_party_manifests "${ROOT}/garrysmod/lua/gincy/*/manifest.lua")
foreach(path IN LISTS first_party_manifests)
    file(READ "${path}" source)
    set(updated "${source}")
    string(REGEX REPLACE "(return \\{id=\"[a-z0-9_]+\",name=\"[^\"]*\",author=\"[^\"]*\",)version=\"[^\"]*\"" "\\1version=\"${PRODUCT}\"" updated "${updated}")
    string(REGEX REPLACE "requires=\\{[^}]*\\}" "requires={gincy=\"${REQUIRES_GINCY}\",api=\"${REQUIRES_API}\"}" updated "${updated}")
    string(REGEX MATCH "dependencies=\\{[^}]*\\}" deps "${updated}")
    if(deps)
        string(REGEX REPLACE "=\"[^\"]*\"" "=\"${REQUIRES_GINCY}\"" new_deps "${deps}")
        string(REPLACE "${deps}" "${new_deps}" updated "${updated}")
    endif()
    emit("${path}" "${updated}")
endforeach()

set(pyproject "${ROOT}/sdk/pyproject.toml")
if(EXISTS "${pyproject}")
    file(READ "${pyproject}" source)
    string(REGEX REPLACE "\nversion = \"[^\"]*\"" "\nversion = \"${PRODUCT}\"" source "${source}")
    emit("${pyproject}" "${source}")
endif()
if(EXISTS "${ROOT}/sdk/gincy_sdk")
    emit("${ROOT}/sdk/gincy_sdk/__init__.py" "# Legacy optional developer tooling. Version values mirror version.json.
__version__ = '${PRODUCT}'
FRAMEWORK_API = '${FRAMEWORK_API}'
PACKAGE_FORMAT = ${PACKAGE_FORMAT}
NATIVE_ABI = ${NATIVE_ABI}
NETWORK_PROTOCOL = ${NETWORK_PROTOCOL}
")
endif()

set(validation "${ROOT}/RELEASE-VALIDATION.json")
if(EXISTS "${validation}")
    file(READ "${validation}" source)
    string(JSON source SET "${source}" release "\"${PRODUCT}\"")
    string(JSON source SET "${source}" versions "{\"product\":\"${PRODUCT}\",\"framework_api\":\"${FRAMEWORK_API}\",\"native_abi\":${NATIVE_ABI},\"package_format\":${PACKAGE_FORMAT},\"network_protocol\":${NETWORK_PROTOCOL},\"management_api\":${MANAGEMENT_API},\"content_schema_api\":${CONTENT_SCHEMA_API}}")
    emit("${validation}" "${source}\n")
endif()

if(CHECK)
    if(drift)
        message(FATAL_ERROR "Derived release metadata is out of date; run `cmake -P tools/release.cmake`:${drift}")
    endif()
    message(STATUS "release metadata matches version.json (${PRODUCT})")
endif()
