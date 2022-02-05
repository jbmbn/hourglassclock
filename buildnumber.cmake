#basic definitions
set(CACHE_FILE "${PROJECT_DIR}/version.txt")
set(BIN_FILE "${BUILD_DIR}/${PROJECT_NAME}.bin")
string(TIMESTAMP TS "%y")

#Reading data from file + incrementation
IF(EXISTS ${CACHE_FILE})
    file(READ ${CACHE_FILE} VERSIONSTR)
    string(SUBSTRING "${VERSIONSTR}" 2 3 VERSION)
    math(EXPR VERSION "${VERSION}+1")
    string(LENGTH "000${VERSION}" LEN)
    math(EXPR START "${LEN}-3")
    string(SUBSTRING "000${VERSION}" ${START} 3 VERSION)
ELSE()
    set(VERSION "001")
ENDIF()

message("${BIN_FILE}")

#Update the cache when bin file is older than version.txt
#This way we'll only reserve a new number after a successful compile
IF(EXISTS ${BIN_FILE})
    IF(${BIN_FILE} IS_NEWER_THAN ${CACHE_FILE})
        #bin file is newer that version file so a new version is needed
        file(WRITE ${CACHE_FILE} "${TS}${VERSION}")
    ENDIF()
ELSE()
    #bin file does not exist. Update version file
    file(WRITE ${CACHE_FILE} "${TS}${VERSION}")
ENDIF()

