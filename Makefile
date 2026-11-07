TARGET = ArrowSurvival
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LIBS = -lpspdebug -lpspdisplay -lpspge -lpspctrl -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Arrow Survival
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
