NAME = tg3_ctrl
PROGRAM = $(NAME).exe
OBJECTS=$(NAME).o 

CXXFLAGS= -std=c++11 -march=native -msse2 -O2

LDFLAGS=-lopencv_calib3d.dll -lopencv_core.dll -lopencv_features2d.dll -lopencv_flann.dll -lopencv_highgui.dll -lopencv_imgcodecs.dll -lopencv_imgproc.dll -lopencv_ml.dll -lopencv_objdetect.dll -lopencv_photo.dll -lopencv_shape.dll -lopencv_stitching.dll -lopencv_superres.dll -lopencv_video.dll -lopencv_videoio.dll -lopencv_videostab.dll -lm -lws2_32 -lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt 

.SUFFIXES: .cpp .o

.PHONY: all
all: $(PROGRAM) 

$(PROGRAM): $(OBJECTS)
	$(CXX)  $^  -o $(PROGRAM) $(LDFLAGS) 

$(OBJECTS): analyze_rtp.h

.c.o: 
	$(CXX) -c $(CXXFLAGS) $<

.PHONY:clean
clean:
	rm -f $(PROGRAM)  *.o
