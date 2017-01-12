SRCFILES= ${wildcard src/*.c}
OBJ     = ${SRCFILES:%.c=%.o}

TARGET   = fake86
BINPATH  =/usr/bin
DATAPATH =/usr/share/fake86
LIBS     =-pthread -lSDL -lX11 -lm -ldl

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LIBS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ -DPATH_DATAFILES=\"$(DATAPATH)/\" $(CFLAGS)
	
install:
	mkdir -p $(DESTDIR)/$(BINPATH)
	mkdir -p $(DESTDIR)/$(DATAPATH)
	cp -p $(TARGET) $(DESTDIR)/$(BINPATH)
	cp -p data/* $(DESTDIR)/$(DATAPATH)

clean:
	$(RM) -f src/*.o src/*~ $(TARGET)

uninstall:
	$(RM) -fr $(DESTDIR)/$(BINPATH)/$(TARGET) $(DESTDIR)/$(DATAPATH)
