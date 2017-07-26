// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Apr  9 17:05:25 2015

#include <ostream>
#include <dirent.h>                    // opendir
#include <fcntl.h>                     // O_RDONLY
#include "base/fd_guard.h"
#include "base/fd_utility.h"

#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/controller.h"           // Controller
#include "brpc/builtin/common.h"
#include "brpc/builtin/dir_service.h"


namespace brpc {

void DirService::default_method(::google::protobuf::RpcController* cntl_base,
                                const ::brpc::DirRequest*,
                                ::brpc::DirResponse*,
                                ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    std::string open_path;
    
    const std::string& path_str =
        cntl->http_request().unresolved_path();
    if (!path_str.empty()) {
        open_path.reserve(path_str.size() + 2);
        open_path.push_back('/');
        open_path.append(path_str);
    } else {
        open_path = "/";
    }
    DIR* dir = opendir(open_path.c_str());
    if (NULL == dir) {
        base::fd_guard fd(open(open_path.c_str(), O_RDONLY));
        if (fd < 0) {
            cntl->SetFailed(errno, "Cannot open `%s'", open_path.c_str());
            return;
        }
        base::make_non_blocking(fd);
        base::make_close_on_exec(fd);

        base::IOPortal read_portal;
        size_t total_read = 0;
        do {
            const ssize_t nr = read_portal.append_from_file_descriptor(
                fd, MAX_READ);
            if (nr < 0) {
                cntl->SetFailed(errno, "Cannot read `%s'", open_path.c_str());
                return;
            }
            if (nr == 0) {
                break;
            }
            total_read += nr;
        } while (total_read < MAX_READ);
        base::IOBuf& resp = cntl->response_attachment();
        resp.swap(read_portal);
        if (total_read >= MAX_READ) {
            std::ostringstream oss;
            oss << " <" << lseek(fd, 0, SEEK_END) - total_read << " more bytes>";
            resp.append(oss.str());
        }
        cntl->http_response().set_content_type("text/plain");
    } else {
        const bool use_html = UseHTML(cntl->http_request());
        const base::EndPoint* const html_addr = (use_html ? Path::LOCAL : NULL);
        cntl->http_response().set_content_type(
            use_html ? "text/html" : "text/plain");

        std::vector<std::string> files;
        files.reserve(32);
        struct dirent ent;
        for (struct dirent* p = &ent; readdir_r(dir, &ent, &p) == 0 && p; ) {
            files.push_back(p->d_name);
        }
        CHECK_EQ(0, closedir(dir));
        
        std::sort(files.begin(), files.end());
        base::IOBufBuilder os;
        if (use_html) {
            os << "<!DOCTYPE html><html><body><pre>";
        }
        std::string str1;
        std::string str2;
        for (size_t i = 0; i < files.size(); ++i) {
            if (path_str.empty() && files[i] == "..") {
                // back to /index
                os << Path("", html_addr, files[i].c_str()) << '\n';
            } else {
                str1 = open_path;
                AppendFileName(&str1, files[i]);
                str2 = "/dir";
                str2.append(str1);
                os << Path(str2.c_str(), html_addr, files[i].c_str()) << '\n';
            }
        }
        if (use_html) {
            os << "</pre></body></html>";
        }
        os.move_to(cntl->response_attachment());
    }
}


} // namespace brpc
