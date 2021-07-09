#pragma once

#include "fastring.h"

// This library is ported from golang's path package.
// Assume the path separator is '/'.

namespace path {

// Return the shortest path name equivalent to path.
fastring clean(const fastring& s);

namespace _xx {
inline fastring join(const fastring& s) {
    return s;
}

template <typename ...S>
inline fastring join(const fastring& x, const S&... s) {
    return !x.empty() ? (x  + "/" + join(s...)) : join(s...);
}
} // namespace _xx

inline fastring join(const fastring& s, const fastring& t) {
    fastring v(!s.empty() ? s + "/" + t : t);
    return !v.empty() ? clean(v) : v;
}

// Join any number of path elements into a single path.
// The result is cleaned. In particular, all empty strings are ignored.
template <typename ...S>
inline fastring join(const S&... s) {
    fastring v = _xx::join(s...);
    return !v.empty() ? clean(v) : v;
}

// Split path by the final slash, separating it into a dir and file name.
// If there is no slash in path, return an empty dir and file set to path.
// The returned values have the property that path = dir+file.
inline std::pair<fastring, fastring> split(const fastring& s) {
    size_t p = s.rfind('/');
    if (p == s.npos) return std::make_pair(fastring(), s);
    return std::make_pair(s.substr(0, p + 1), s.substr(p + 1));
};

// Return the dir part of the path. The result is cleaned.
// If the path is empty, return ".".
inline fastring dir(const fastring& s) {
    return clean(split(s).first);
}

// Return the last element of path.
// Trailing slashes are removed before extracting the last element.
// If the path is empty, return ".".
// If the path consists entirely of slashes, return "/".
fastring base(const fastring& s);

// Return file name extension used by path.
// path::ext("a.b/x.log")  ->  ".log"
fastring ext(const fastring& s);

// return all files in path ,except the filter directory
bool getAllFiles(const std::string path, std::vector<std::string>& files, const std::vector<std::string> filter_directory = std::vector<std::string>());

// return all formats files in path ,except the filter directory
bool getAllFormatFiles(const std::string path, std::vector<std::string>& files, std::string format, const std::vector<std::string> filter_directory = std::vector<std::string>());

inline bool getAllFormatFiles(const std::vector<std::string> path, std::vector<std::string>& files, std::string format, const std::vector<std::string> filter_directory = std::vector<std::string>()){
	if(path.size()<=0) return false;
	for (size_t i = 0; i<path.size(); i++){
			getAllFormatFiles(path[i], files, format, filter_directory);
	}
	return true;
}

} // namespace path
