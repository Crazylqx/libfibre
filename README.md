libfibre
========

libfibre is an M:N user-threading runtime without preemption, thus the term <i>fibre</i>.

Running `make` builds the fibre library (static and shared) along with several example/test programs: `test1`, `ordering`, `threadtest`, `echotest`, and `webserver`.

Both Linux/epoll and FreeBSD/kqueue are supported, but significantly more testing has been done for Linux/epoll.

### Collaborators

The runtime has originally been developed in close collaboration with Saman Barghi. All bugs are mine though. ;-)

### Contributors

The following students (in alphabetical order) have helped with various parts of libfibre:

- Qin An (FreeBSD/kqueue)
- Bilal Akhtar (gdb extension)
- Peng Chen (split stack support)
- Wen Shi (gdb extension)
- Shuangyi Tong (multiple event scopes)
