#include "ofApp.h"

#include <future>

// note
// binary format of GV
// 0: uint32_t width
// 4: uint32_t height
// 8: uint32_t frame count
// 12: float fps
// 16: uint32_t fmt (DXT1 = 1, DXT3 = 3, DXT5 = 5, BC7 = 7)
// 20: uint32_t frame bytes
// 24: raw frame storage (lz4 compressed)
// eof - (frame count) * 16: [(uint64_t, uint64_t)..<frame count] (address, size) of lz4, address is zero based from file head

//--------------------------------------------------------------
void ofApp::setup(){
    gui.setup();

    // detect cores
    cores = std::thread::hardware_concurrency();
}

//--------------------------------------------------------------
void ofApp::update(){

}

//--------------------------------------------------------------
void ofApp::startEncodeThread() {
    ofxAsync::run([&]() {
        encodeStarted = true;

        // Sort files by name
        std::sort(sourceDirPaths.begin(), sourceDirPaths.end());

        // Create empty progress map
        progressMap.clear();
        for (const auto& path : sourceDirPaths) {
            progressMap[path] = 0;
        }

        ofLogNotice("ofApp") << "Start Encode";

        for (const auto& sourceDirPath : sourceDirPaths) {
            if (needExit) break;

            ofDirectory sourceDir(sourceDirPath);
            sourceDir.allowExt("jpg");
            sourceDir.allowExt("jpeg");
            sourceDir.allowExt("png");
            sourceDir.allowExt("gif");
            sourceDir.allowExt("bmp");
            sourceDir.listDir();

            string targetPath = sourceDirPath + ".gv";
            ofFile::removeFile(targetPath);

            ofstream fp(targetPath, ios::binary);
            if (!fp.is_open()) {
                ofLogError("ofApp") << "Failed to open file: " << targetPath;
                continue;
            }

            // Write header (same as before)
            writeHeader(fp, sourceDir.getPath(0), sourceDir.size());

            vector<pair<uint64_t, uint64_t>> address_and_sizes;
            std::mutex file_mutex;
            std::atomic<int> processed_frames(0);

            // Process frames
            vector<string> framePaths;
            for (int i = 0; i < sourceDir.size(); ++i) {
                framePaths.push_back(sourceDir.getPath(i));
            }

            processFramesInParallel(framePaths, cores, [&](const string& path) {
                ofPixels pixels;
                ofLoadImage(pixels, path);

                if (pixels.getNumChannels() == 3) {
                    pixels = convertToRGBA(pixels);
                }

                ofBuffer lz4Buf = serializer.serializeImageToLZ4(pixels);

                std::lock_guard<std::mutex> lock(file_mutex);
                uint64_t address = fp.tellp();
                fp.write(lz4Buf.getData(), lz4Buf.size());
                address_and_sizes.emplace_back(address, lz4Buf.size());

                int current = ++processed_frames;
                float progress = (float)(current) / sourceDir.size() * 100;
                progressMap[sourceDirPath] = progress;
            });

            // Write address and sizes (same as before)
            writeAddressAndSizes(fp, address_and_sizes);

            // If aborted, fix frameCount (same as before)
            if (needExit) {
                fixFrameCount(fp, address_and_sizes.size());
            }

            fp.close();
            dones.push_back(sourceDirPath);
        }

        // Clear sourceDirPaths
        sourceDirPaths.clear();

        ofLogNotice("ofApp") << "End Encode";
        encodeStarted = false;
    });
}

// Helper functions

void ofApp::writeHeader(ofstream& fp, const string& firstFramePath, int frameCount) {
    ofPixels firstPixels;
    ofLoadImage(firstPixels, firstFramePath);

    uint32_t width = firstPixels.getWidth();
    uint32_t height = firstPixels.getHeight();
    uint32_t fmt = 5; // DXT5
    uint32_t frameBytes = width * height;

    fp.write(reinterpret_cast<char*>(&width), sizeof(uint32_t));
    fp.write(reinterpret_cast<char*>(&height), sizeof(uint32_t));
    fp.write(reinterpret_cast<char*>(&frameCount), sizeof(uint32_t));
    fp.write(reinterpret_cast<char*>(&fps), sizeof(float));
    fp.write(reinterpret_cast<char*>(&fmt), sizeof(uint32_t));
    fp.write(reinterpret_cast<char*>(&frameBytes), sizeof(uint32_t));
}

ofPixels ofApp::convertToRGBA(const ofPixels& pixels) {
    ofPixels alphaPixels;
    alphaPixels.allocate(pixels.getWidth(), pixels.getHeight(), 1);
    std::fill(alphaPixels.getData(), alphaPixels.getData() + alphaPixels.size(), 255);

    ofPixels rgbaPixels;
    rgbaPixels.setFromPixels(pixels.getData(), pixels.getWidth(), pixels.getHeight(), OF_IMAGE_COLOR_ALPHA);
    rgbaPixels.setChannel(3, alphaPixels);
    return rgbaPixels;
}

void ofApp::processFramesInParallel(const vector<string>& framePaths, int numCores, const std::function<void(const string&)>& processFrame) {
    for (size_t i = 0; i < framePaths.size(); i += numCores) {
        vector<std::future<void>> futures;
        for (int j = 0; j < numCores && i + j < framePaths.size(); ++j) {
            futures.push_back(std::async(std::launch::async, processFrame, framePaths[i + j]));
        }
        for (auto& future : futures) {
            future.wait();
        }
    }
}

void ofApp::writeAddressAndSizes(ofstream& fp, const vector<pair<uint64_t, uint64_t>>& address_and_sizes) {
    for (const auto& [address, size] : address_and_sizes) {
        fp.write(reinterpret_cast<const char*>(&address), sizeof(uint64_t));
        fp.write(reinterpret_cast<const char*>(&size), sizeof(uint64_t));
    }
}

void ofApp::fixFrameCount(ofstream& fp, size_t actualFrameCount) {
    uint32_t frameCount = actualFrameCount;
    fp.seekp(8);
    fp.write(reinterpret_cast<char*>(&frameCount), sizeof(uint32_t));
}

//--------------------------------------------------------------
void ofApp::draw(){
    // source dir chooser, imgui

    if (encodeStarted) {
        // show progress

        gui.begin();
        {
            ImGui::Begin("Progress");
            {
                for (auto it = progressMap.begin(); it != progressMap.end(); it++) {
                    ImGui::Text("%s: %0.1f %%", it->first.c_str(), it->second);
                }
            }
            ImGui::End();
        }
        gui.end();
    }else{

        gui.begin();
        {
            ImGui::Begin("Source Directories");
            {
                if (ImGui::Button("Add Source Directory")) {
                    ofFileDialogResult result = ofSystemLoadDialog("Choose Source Directory", true);
                    if (result.bSuccess) {
                        sourceDirPaths.push_back(result.filePath);
                    }
                }

                for (int i = 0; i < sourceDirPaths.size(); i++) {
                    ImGui::Text("%s", sourceDirPaths[i].c_str());
                }
            }
            ImGui::End();

            ImGui::Begin("Dones");
            {
                for (int i = 0; i < dones.size(); i++) {
                    ImGui::Text("%s", dones[i].c_str());
                }
            }
            ImGui::End();

            // set cores
            ImGui::Begin("Encoder Settings");
            {
                ImGui::DragInt("Cores", &cores, 1, 1, 64);
            }
            ImGui::End();

            ImGui::Begin("Encode");
            {
                // fps
                ImGui::DragFloat("FPS", &fps, 1.0f, 1.0f, 360.0f);
                if (ImGui::Button("Start Encode")) {
                    startEncodeThread();
                }
            }
            ImGui::End();
        }
        gui.end();
    }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 
    // if directory, add to sourceDirPaths

    if(!encodeStarted){
        if (dragInfo.files.size() > 0) {
            for (int i = 0; i < dragInfo.files.size(); i++) {
                std::string path = dragInfo.files[i];
                ofFile file(path);
                if (file.isDirectory()) {
                    sourceDirPaths.push_back(path);
                }
            }
        }
    }
}

// --------------------------------------------------------------
void ofApp::exit() {
    needExit = true;
    ofxAsync::stopAll();
}
