include ../Makefile.config
SOURCEDIRS=runtime libfibre
SOURCES=$(wildcard $(addsuffix /*.cc,$(SOURCEDIRS)))
OBJECTS=$(subst .cc,.o,$(notdir $(SOURCES)))
CSOURCES=$(wildcard $(addsuffix /*.c,$(SOURCEDIRS))) errnoname/errnoname.c
COBJECTS=$(subst .c,.o,$(notdir $(CSOURCES)))
ASOURCES=$(wildcard $(addsuffix /*.S,$(SOURCEDIRS)))
AOBJECTS=$(subst .S,.o,$(notdir $(ASOURCES)))

DEPENDS=$(subst .cc,.d,$(notdir $(SOURCES))) $(subst .c,.d,$(notdir $(CSOURCES))) $(subst .S,.d,$(notdir $(ASOURCES)))
DEPENDS+=$(subst .c,.d,$(notdir $(CSOURCES)))

LIBA=libfibre.a
LIBSO=libfibre.so

CFLAGS+=-I. -D__LIBFIBRE__

vpath %.cc $(SOURCEDIRS)
vpath %.c errnoname
vpath %.S $(SOURCEDIRS)

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
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OBJECTS) errnoname/errnoname.c: errnoname/errnoname.h

errnoname/errnoname.h:
	git submodule update --init errnoname

clean:
	rm -f $(LIBA) $(LIBSO) $(OBJECTS) $(COBJECTS) $(AOBJECTS) $(DEPENDS)

-include $(DEPENDS)
