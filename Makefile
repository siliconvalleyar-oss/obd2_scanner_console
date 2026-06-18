CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -MMD -MP -fPIC -Wno-unused-parameter
LDFLAGS := -lbluetooth -lpthread

SRCDIR := src
OBJDIR := obj
BINDIR := bin

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

TARGET := $(BINDIR)/elm327_console

.PHONY: all clean run mkdirs

all: mkdirs $(TARGET)

mkdirs:
	@mkdir -p $(OBJDIR) $(BINDIR)

$(TARGET): $(OBJS)
	@echo "=== Vinculando $@ ==="
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Compilacion exitosa!"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -rf $(OBJDIR)/*.o $(OBJDIR)/*.d $(BINDIR)/*

-include $(DEPS)
