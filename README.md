libfibre
========

libfibre is an M:N user-threading libray without preemption, thus the term
<i>fibre</i>.

For now, the project lives in the <tt>src/libfibre</tt> subdirectory of the
[KOS project](https://git.uwaterloo.ca/mkarsten/KOS).

Running `make` in <tt>src/libfibre</tt> should build the fibre library and a
four example programs: `test1`, `ordering`, `threadtest`, `echotest`, and `webserver`.

Currently, only Linux/epoll is well-tested, but FreeBSD/kqueue is also supported.
