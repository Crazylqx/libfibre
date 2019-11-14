include ../Makefile.config
SOURCEDIRS=runtime libfibre
SOURCES=$(wildcard $(addsuffix /*.cc,$(SOURCEDIRS)))
OBJECTS=$(subst .cc,.o,$(notdir $(SOURCES)))
DEPENDS=$(subst .cc,.d,$(notdir $(SOURCES)))
ASOURCES=$(wildcard $(addsuffix /*.S,$(SOURCEDIRS)))
AOBJECTS=$(subst .S,.o,$(notdir $(ASOURCES)))

LIBA=libfibre.a
LIBSO=libfibre.so

CXXFLAGS+=-I. -D__LIBFIBRE__

vpath %.cc $(SOURCEDIRS)
vpath %.S $(SOURCEDIRS)

AFLAGS=-g --divide -I$(SRCDIR) --defsym __$(shell uname -m)__=1

.PHONY: all clean

all: $(LIBA) $(LIBSO)

$(LIBA): $(OBJECTS) $(AOBJECTS)
	$(AR) cr $@ $^
	ranlib $@

$(LIBSO): $(OBJECTS) $(AOBJECTS)
	$(CXX) -shared -pthread $^ -o $@

# also creates dependencies
$(OBJECTS): %.o: %.cc
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(AOBJECTS): %.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -f $(LIBA) $(LIBSO) $(OBJECTS) $(DEPENDS) $(AOBJECTS)

-include $(DEPENDS)
