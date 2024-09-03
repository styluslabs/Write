# Makefile for resources (icons, strings)
# GNU make from v. 3.82 until 4.3 did not return sorted values for wildcard [use $(sort $(wildcard ...))]

# remove target if building fails
.DELETE_ON_ERROR:

.PHONY: all clean

all: res_icons.cpp res_strings.cpp

res_icons.cpp: icons/*.svg
	python3 embed.py $^ > $@

res_strings.cpp: strings/*.xml
	python3 embed.py --compress $^ > $@

clean:
	rm res_icons.cpp res_strings.cpp
