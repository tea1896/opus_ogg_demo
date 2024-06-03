gcc -o opus opus.c \
-I/third_lib/opus-1.5.2/output/include \
-L/third_lib/opus-1.5.2/output/lib \
-I/third_lib/libogg-1.3.5/output/include \
-L/third_lib/libogg-1.3.5/output/lib \
-lopus -logg -lm -ggdb 

