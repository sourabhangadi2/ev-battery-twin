# Makefile — EV Battery Digital Twin C Server
# Uses MinGW64 GCC on Windows (MSYS2)

CC      = C:/msys64/mingw64/bin/gcc.exe
MAKE    = C:/msys64/mingw64/bin/mingw32-make.exe

TARGET  = battery_server.exe

OPENSSL_INC = C:/msys64/mingw64/include
OPENSSL_LIB = C:/msys64/mingw64/lib

CFLAGS  = -Wall -Wextra -O2 \
           -I. \
           -I$(OPENSSL_INC) \
           -DSQLITE_THREADSAFE=0 \
           -DSQLITE_OMIT_LOAD_EXTENSION \
           -D_WIN32_WINNT=0x0600

LDFLAGS = -L$(OPENSSL_LIB) \
           -lssl -lcrypto \
           -lws2_32 -lgdi32 -lbcrypt \
           -lm

SRCS = main.c \
       sqlite3.c \
       json.c \
       database.c \
       auth.c \
       twin_engine.c \
       healing_engine.c \
       http_server.c \
       router.c \
       routes/route_auth.c \
       routes/route_sensor.c \
       routes/route_command.c \
       routes/route_static.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo ""
	@echo "Build successful! Run with: .\battery_server.exe"
	@echo ""

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	del /Q *.o routes\*.o $(TARGET) 2>nul || true
