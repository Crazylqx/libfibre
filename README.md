libfibre
========

libfibre is an M:N user-threading runtime without preemption, thus the term <i>fibre</i>.

Running `make` builds the fibre library in `src/libfibre.so` along with several example/test programs: `test1`, `ordering`, `threadtest`, `echotest`, and `webserver` in the subdirectory `apps`.

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

### License

libfibre is currently distributed under the GNU GPL license, although this could change in the future. See <tt>[LICENSE](LICENSE)</tt> for details.

### Feedback / Questions

Please send any questions or feedback to mkarsten|**at**|uwaterloo.ca.
