#include "co/path.h"
#include <regex>

#if defined(__cplusplus) && __cplusplus >= 201703L 
#if defined(__has_include) || __has_include(<filesystem>)
#include <filesystem>
namespace fsm = std::filesystem;
#endif
#else
#include <experimental\filesystem>
namespace fsm = std::experimental::filesystem;
#endif

namespace path {

fastring clean(const fastring& s) {
    if (s.empty()) return fastring(1, '.');

    fastring r(s.data(), s.size());
    size_t n = s.size();

    bool rooted = s[0] == '/';
    size_t p = rooted ? 1 : 0;      // index for string r
    size_t dotdot = rooted ? 1 : 0; // index where .. must stop

    for (size_t i = p; i < n;) {
        if (s[i] == '/' || (s[i] == '.' && (i+1 == n || s[i+1] == '/'))) {
            // empty or . element
            ++i;

        } else if (s[i] == '.' && s[i+1] == '.' && (i+2 == n || s[i+2] == '/')) {
            // .. element: remove to last /
            i += 2;

            if (p > dotdot) {
                // backtrack
                for (--p; p > dotdot && r[p] != '/'; --p);

            } else if (!rooted) {
                // cannot backtrack, but not rooted, so append .. element
                if (p > 0) r[p++] = '/';
                r[p++] = '.';
                r[p++] = '.';
                dotdot = p;
            }

        } else {
            // real path element, add slash if needed
            if ((rooted && p != 1) || (!rooted && p != 0)) {
                r[p++] = '/';
            }

            // copy element until the next /
            for (; i < n && s[i] != '/'; i++) {
                r[p++] = s[i];
            }
        }
    }

    if (p == 0) return fastring(1, '.');
    return r.substr(0, p);
}

fastring base(const fastring& s) {
    if (s.empty()) return fastring(1, '.');

    size_t p = s.size();
    for (; p > 0; p--) {
        if (s[p - 1] != '/') break;
    }
    if (p == 0) return fastring(1, '/');

    fastring x = (p == s.size() ? s : s.substr(0, p));
    size_t c = x.rfind('/');
    return c != x.npos ? x.substr(c + 1) : x;
}

fastring ext(const fastring& s) {
    for (size_t i = s.size() - 1; i != (size_t)-1 && s[i] != '/'; --i) {
        if (s[i] == '.') return s.substr(i);
    }
    return fastring();
}

bool getAllFiles(const std::string path, std::vector<std::string>& files, const std::vector<std::string> filter_directory){
	fsm::path file_path = path;
	if(path.empty() || !fsm::exists(file_path)){
		files.clear();
		return false;
	}
	if(fsm::is_directory(file_path)){
		for (auto f : fsm::recursive_directory_iterator(file_path)){
			bool foundFlag = false;
			for (size_t i = 0; i < filter_directory.size(); i++){
				fsm::path filter_directory_index = filter_directory[i];
				std::string::size_type idx = f.path().string().find(filter_directory_index.string());
				if (idx != std::string::npos){
						foundFlag = true;
						break;
				}
			}
			if(foundFlag == false){
				if (!fsm::is_directory(f)) files.push_back(f.path().string());
			}
		}
	}
	else{
		files.push_back(path)
	}
	return true;
}

bool getAllFormatFiles(const std::string path, std::vector<std::string>& files, std::string expression, const std::vector<std::string> filter_directory){
		fsm::path file_path = path;
		if(path.empty() || !fsm::exists(file_path)){
			files.clear();
			return false;
		}
		std::regex Img(expression, std::regex_constants::syntax_option_type::icase);
		if (fsm::is_directory(file_path)){
			for (auto f : fsm::recursive_directory_iterator(file_path)){
				int foundFlag = false;																						//过滤文件夹
				for (size_t i = 0; i < filter_directory.size(); i++){
					fsm::path filter_directory_index = filter_directory[i];
					std::string::size_type idx = f.path().string().find(filter_directory_index.string());
					if (idx != string::npos){
						foundFlag = true;
						break;
					}
				}
				if (foundFlag == false){
					auto fname = f.path().filename().string();
					if (std::regex_match(fname, Img)){
						files.push_back(f.path().string());
					}
				}
			}
		}
		else{
			if (std::regex_match(fs::path(path).filename().string(), Img)){
				files.push_back(path);
			}
		}
		return true;
}

} // namespace path
