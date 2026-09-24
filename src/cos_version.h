/**
 * @file cos_version.h
 * @brief Version definitions
 *
 * Follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
 */

#ifndef COS_VERSION_H
#define COS_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Version Definitions ---------------------------------------------------*/

#define CANTOMK6_OS_VERSION_MAJOR 1
#define CANTOMK6_OS_VERSION_MINOR 0
#define CANTOMK6_OS_VERSION_PATCH 0
#define CANTOMK6_OS_VERSION_INFO ""

#define CANTOMK6_OS_API_LEVEL CANTOMK6_OS_VERSION_MAJOR

#define STRINGIFY(x) #x
/* info 需自带连字符前缀(如 "-beta"),为空则显示纯 "x.y.z" */
#define VERSION_STRING(major, minor, patch, info) STRINGIFY(major) "." STRINGIFY(minor) "." STRINGIFY(patch) info

#define CANTOMK6_OS_VERSION_FULL \
    VERSION_STRING(CANTOMK6_OS_VERSION_MAJOR, CANTOMK6_OS_VERSION_MINOR, CANTOMK6_OS_VERSION_PATCH, CANTOMK6_OS_VERSION_INFO)

#ifdef __cplusplus
}
#endif

#endif /* COS_VERSION_H */
