libfibre
========

libfibre is an M:N user-threading libray without preemption, thus the term <i>fibre</i>.

For now, the project lives in the <tt>src/libfibre</tt> subdirectory of the [KOS project](https://git.uwaterloo.ca/mkarsten/KOS).

Running `make` in <tt>src/libfibre</tt> builds the fibre library (static and shared) along with several example/test programs: `test1`, `ordering`, `threadtest`, `echotest`, and `webserver`.

Both Linux/epoll and FreeBSD/kqueue are supported, but significantly more testing has been done for Linux/epoll.

