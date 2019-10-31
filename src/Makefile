include ../Makefile.config
SOURCEDIRS=runtime libfibre
SOURCES=$(wildcard $(addsuffix /*.cc,$(SOURCEDIRS)))
OBJECTS=$(subst .cc,.o,$(notdir $(SOURCES)))
ASOURCES=$(wildcard $(addsuffix /*.S,$(SOURCEDIRS)))
AOBJECTS=$(subst .S,.o,$(notdir $(ASOURCES)))

LIBA=libfibre.a
LIBSO=libfibre.so

CXXFLAGS+=-I. -D__LIBFIBRE__

vpath %.cc $(SOURCEDIRS)
vpath %.S $(SOURCEDIRS)

AFLAGS=-g --divide -I$(SRCDIR) --defsym __$(shell uname -m)__=1

all: $(LIBA) $(LIBSO)

$(LIBA): $(OBJECTS) $(AOBJECTS)
	$(AR) cr $@ $^
	ranlib $@

$(LIBSO): $(OBJECTS) $(AOBJECTS)
	$(CXX) -shared -pthread $^ -o $@

$(OBJECTS): %.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(AOBJECTS): %.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean vclean dep depend

clean:
	rm -f $(LIBA) $(LIBSO) $(OBJECTS) $(AOBJECTS)

vclean: clean
	rm -f Makefile.dep

dep depend Makefile.dep:
	$(CXX) -MM $(CXXFLAGS) -D__LIBFIBRE__ $(SOURCES) > Makefile.dep

-include Makefile.dep
