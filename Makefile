#(c) lixiang 2019-2021 <lixiang@kylinos.cn>

PREFIX = /usr
LIBDIR = $(PREFIX)/lib

#LDFLAGS = `pkg-config --cflags --libs protobuf-lite protobuf`
LDFLAGS = `pkg-config --cflags --libs protobuf`
CC            = g++
targets = libkmre.so

all:
	protoc -I=./ --cpp_out=./ KmreCore.proto
	$(CC) -fPIC -shared main.cc kmre_socket.cc KmreCore.pb.cc -std=c++14 -fpermissive -g -o ${targets} $(LDFLAGS) -ldl

.PHONY : uninstall
.PHONY : clean

#install:
#	@echo $(DESTDIR)$(LIBDIR)
#	-install ${targets} $(DESTDIR)$(LIBDIR)
#	@echo "Makefile: libkmre.so installed."

uninstall:
	rm -rf $(DESTDIR)$(LIBDIR)/${targets}

clean:
	rm -f *.o
	rm -f KmreCore.pb.*
	rm -f ${targets}
