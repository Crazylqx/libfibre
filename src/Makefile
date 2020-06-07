include ../Makefile.config
SOURCEDIRS=runtime libfibre errnoname
SOURCES=$(wildcard $(addsuffix /*.cc,$(SOURCEDIRS)))
OBJECTS=$(subst .cc,.o,$(notdir $(SOURCES)))
CSOURCES=$(wildcard $(addsuffix /*.c,$(SOURCEDIRS)))
COBJECTS=$(subst .c,.o,$(notdir $(CSOURCES)))
ASOURCES=$(wildcard $(addsuffix /*.S,$(SOURCEDIRS)))
AOBJECTS=$(subst .S,.o,$(notdir $(ASOURCES)))

DEPENDS=$(subst .cc,.d,$(notdir $(SOURCES)))
DEPENDS+=$(subst .c,.d,$(notdir $(CSOURCES)))

LIBA=libfibre.a
LIBSO=libfibre.so

CXXFLAGS+=-I. -D__LIBFIBRE__

vpath %.cc $(SOURCEDIRS)
vpath %.c $(SOURCEDIRS)
vpath %.S $(SOURCEDIRS)

AFLAGS=-g --divide -I$(SRCDIR) --defsym __$(shell uname -m)__=1

.PHONY: all clean

all: $(LIBA) $(LIBSO)

$(LIBA): $(OBJECTS) $(COBJECTS) $(AOBJECTS)
	$(AR) cr $@ $^
	ranlib $@

$(LIBSO): $(OBJECTS) $(COBJECTS) $(AOBJECTS)
	$(CXX) -shared -pthread $^ -o $@

# also creates dependencies
$(OBJECTS): %.o: %.cc
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(COBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(AOBJECTS): %.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -f $(LIBA) $(LIBSO) $(OBJECTS) $(COBJECTS) $(AOBJECTS) $(DEPENDS)

-include $(DEPENDS)
