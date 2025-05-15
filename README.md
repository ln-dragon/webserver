# webserver<br>
## The web site which is fixed by dragon is running!<br>
先查看网站根目录的地址是否为本地地址，不是就要修改为本地地址，在src/http_conn.cpp文件中14行
// 网站的根目录
const char* doc_root = "/home/dragon/webserver/resources";
编译方式<br>`cd build/`<br>`cmake ..  make`<br>
运行服务器<br>`cd bin/`<br>`./mywebserver 主机IP 端口号` 或者 `./mywebserver_pro 主机IP 端口号`<br>
浏览器访问<br>`http://主机号:端口号/index.html`<br>
