include $(PSL1GHT)/ppu_rules

TARGET   := cobra
SOURCES  := .
INCLUDES := .
CFLAGS   := -O2 -Wall
LIBS     := -ltiny3d -lgcm_sys -lsysutil -lio -lm