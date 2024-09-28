#include "ofApp.h"

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
}

//--------------------------------------------------------------
void ofApp::update(){

}

//--------------------------------------------------------------
void ofApp::startEncodeThread() {
    ofxAsync::run([&](){
        encodeStarted = true;

        // first, sort files by name
        std::sort(sourceDirPaths.begin(), sourceDirPaths.end());

        // create empty progress map
        progressMap.clear();
        for (int i = 0; i < sourceDirPaths.size(); i++) {
            progressMap[sourceDirPaths[i]] = 0;
        }

        ofLogNotice("ofApp") << "Start Encode";

        for (int i = 0; i < sourceDirPaths.size(); i++) {
            // target path is source path + ".gv"
            std::string sourceDirPath = sourceDirPaths[i];

            ofDirectory sourceDir(sourceDirPath);
            sourceDir.allowExt("jpg");
            sourceDir.allowExt("jpeg");
            sourceDir.allowExt("png");
            sourceDir.allowExt("gif");
            sourceDir.allowExt("bmp");
            sourceDir.listDir();

            // open target 
            std::string targetPath = sourceDirPath + ".gv";
            // if exists, delete
            ofFile::removeFile(targetPath);
            
            // open file pointer with std
            ofstream fp(targetPath, ios::binary);
            if (!fp.is_open()) {
                ofLogError("ofApp") << "Failed to open file: " << targetPath;
                return;
            }

            // get paths and sort
            std::vector<std::string> paths;
            for (int j = 0; j < sourceDir.size(); j++) {
                paths.push_back(sourceDir.getPath(j));
            }
            std::sort(paths.begin(), paths.end());

            // write header
            {
                // open first image to get width, height
                // ofImage firstImage;
                ofPixels firstPixels;
                // ofLogNotice("ofApp") << "Open: " << sourceDir.getPath(0);
                ofLoadImage(firstPixels, paths[0]);

                // write width, height
                uint32_t width = firstPixels.getWidth();
                uint32_t height = firstPixels.getHeight();
                fp.write((char*)&width, sizeof(uint32_t));
                fp.write((char*)&height, sizeof(uint32_t));

                // write frame count
                uint32_t frameCount = paths.size();
                fp.write((char*)&frameCount, sizeof(uint32_t));

                // write fps
                fp.write((char*)&fps, sizeof(float));

                // write fmt
                uint32_t fmt = 5; // DXT5
                fp.write((char*)&fmt, sizeof(uint32_t));

                // write frame bytes
                uint32_t frameBytes = width * height; // because DXT5
                fp.write((char*)&frameBytes, sizeof(uint32_t));
            }

            // clear address and sizes
            address_and_sizes.clear();

            // write frames

            for (int j = 0; j < sourceDir.size(); j++) {
                uint64_t address = fp.tellp();

                std::string sourcePath = paths[j];

                // ofLogNotice("ofApp") << "Encode: " << sourcePath;
                
                ofFile sourceFile(sourcePath);

                ofPixels sourcePixels;
                sourcePixels.setImageType(OF_IMAGE_COLOR_ALPHA);
                ofLoadImage(sourcePixels, sourcePath);

                // if RGB, convert to RGBA
                if (sourcePixels.getNumChannels() == 3) {
                    ofPixels alphaPixels;
                    alphaPixels.allocate(sourcePixels.getWidth(), sourcePixels.getHeight(), 1);
                    // set alpha to 255
                    for (int i = 0; i < alphaPixels.size(); i++) {
                        alphaPixels[i] = 255;
                    }

                    sourcePixels.setImageType(OF_IMAGE_COLOR_ALPHA);
                    sourcePixels.setNumChannels(4);
                    sourcePixels.setChannel(3, alphaPixels);
                }

                ofBuffer lz4Buf = serializer.serializeImageToLZ4(sourcePixels);

                uint64_t size = lz4Buf.size();

                // save address and size
                address_and_sizes.push_back(std::make_pair(address, size));

                // write lz4 compressed frame
                fp.write(lz4Buf.getData(), size);

                float progress = (float)(j + 1) / sourceDir.size() * 100;
                progressMap[sourceDirPath] = progress;
            }

            // write address and sizes
            for (int j = 0; j < address_and_sizes.size(); j++) {
                fp.write((char*)&address_and_sizes[j].first, sizeof(uint64_t));
                fp.write((char*)&address_and_sizes[j].second, sizeof(uint64_t));
            }

            // close file
            fp.close();

            dones.push_back(sourceDirPath);
        }

        // clear sourceDirPaths
        sourceDirPaths.clear();

        ofLogNotice("ofApp") << "End Encode";

        encodeStarted = false;
    });
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
