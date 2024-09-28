#pragma once

#include "ofMain.h"

#include "ofxImGui.h"
#include "ofxAsync.h"
#include "ofxGVTextureSerializer.h"

class ofApp : public ofBaseApp{

	public:
		bool encodeStarted = false;
		vector<std::string> sourceDirPaths;
        vector<std::string> dones;
		float fps = 30;

		bool needExit = false;

		int cores = 4;

        vector<std::pair<uint64_t, uint64_t>> address_and_sizes;

		std::map<std::string, float> progressMap;

		ofxImGui::Gui gui;
		ofxGVTextureSerializer serializer;

		void setup();
		void update();
		void draw();
		void startEncodeThread();

		void keyPressed(int key);
		void keyReleased(int key);
		void mouseMoved(int x, int y );
		void mouseDragged(int x, int y, int button);
		void mousePressed(int x, int y, int button);
		void mouseReleased(int x, int y, int button);
		void mouseEntered(int x, int y);
		void mouseExited(int x, int y);
		void windowResized(int w, int h);
		void dragEvent(ofDragInfo dragInfo);
		void gotMessage(ofMessage msg);

		void exit();

		// helper functions
		void writeHeader(ofstream& fp, const string& firstFramePath, int frameCount);
		void writeAddressAndSizes(ofstream& fp, const vector<pair<uint64_t, uint64_t>>& address_and_sizes);
		void fixFrameCount(ofstream& fp, size_t frameCount);
		// ofPixels convertToRGBA(const ofPixels& pixels);
		void setAlphaPixels(ofPixels& pixels);
		void processFramesInParallel(const vector<string>& framePaths, int numCores, const std::function<void(const string&)>& processFrame);
};
