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

    // detect cores
    cores = std::thread::hardware_concurrency();
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

            // for (int j = 0; j < paths.size(); j++) {
            //     uint64_t address = fp.tellp();

            //     std::string sourcePath = paths[j];

            //     // ofLogNotice("ofApp") << "Encode: " << sourcePath;
                
            //     ofFile sourceFile(sourcePath);

            //     ofPixels sourcePixels;
            //     sourcePixels.setImageType(OF_IMAGE_COLOR_ALPHA);
            //     ofLoadImage(sourcePixels, sourcePath);

            //     // if RGB, convert to RGBA
            //     if (sourcePixels.getNumChannels() == 3) {
            //         ofPixels alphaPixels;
            //         alphaPixels.allocate(sourcePixels.getWidth(), sourcePixels.getHeight(), 1);
            //         // set alpha to 255
            //         for (int i = 0; i < alphaPixels.size(); i++) {
            //             alphaPixels[i] = 255;
            //         }

            //         sourcePixels.setImageType(OF_IMAGE_COLOR_ALPHA);
            //         sourcePixels.setNumChannels(4);
            //         sourcePixels.setChannel(3, alphaPixels);
            //     }

            //     ofBuffer lz4Buf = serializer.serializeImageToLZ4(sourcePixels);

            //     uint64_t size = lz4Buf.size();

            //     // save address and size
            //     address_and_sizes.push_back(std::make_pair(address, size));

            //     // write lz4 compressed frame
            //     fp.write(lz4Buf.getData(), size);

            //     float progress = (float)(j + 1) / sourceDir.size() * 100;
            //     progressMap[sourceDirPath] = progress;
            // }

            // make parallel up to cores
            for (int j = 0; j < paths.size(); j++) {
                // wait processes
                map<int, ofBuffer> lz4Bufs;
                map<int, pair<uint64_t, uint64_t>> address_and_sizes_;
                map<int, ofPixels> sourcePixelMap;
                map<int, bool> flags;
                map<int, bool> error_flags;

                vector<std::string> queue;

                for (int k = 0; k < cores; k++) {
                    if (j + k >= paths.size()) {
                        break;
                    }

                    queue.push_back(paths[j + k]);
                    flags[k] = false;
                }

                // load pixels
                for (int k = 0; k < queue.size(); k++) {
                    std::string sourcePath = queue[k];

                    ofLogNotice("ofApp") << "Encode: " << sourcePath;

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

                    sourcePixelMap[k] = sourcePixels;
                }

                std::mutex mutex;

                for (int k = 0; k < queue.size(); k++) {
                    std::string sourcePath = queue[k];
                    ofPixels sourcePixels = sourcePixelMap[k];

                    ofxAsync::run([&, sourcePath, sourcePixels, k](){
                        try {
                            ofLogNotice("ofApp") << "Encode: " << sourcePath;

                            // check pixel size is not 0
                            if (sourcePixels.size() == 0) {
                                throw std::exception();
                            }

                            ofBuffer lz4Buf = serializer.serializeImageToLZ4(sourcePixels);
                            if (lz4Buf.size() == 0) {
                                throw std::exception();
                            }
                            uint64_t size = lz4Buf.size();

                            mutex.lock();

                            // save address and size
                            address_and_sizes_[k] = std::make_pair(0, size);
                            
                            // save lz4 buffer
                            lz4Bufs[k] = lz4Buf;

                            ofLogNotice("ofApp") << "End Encode: " << sourcePath;

                            flags[k] = true;

                            mutex.unlock();
                        } catch (std::exception e) {
                            error_flags[k] = true;
                            flags[k] = true;
                        }
                    });
                }

                // wait all
                ofLogNotice("ofApp") << "Wait all";
                bool state = false;
                while (!state) {
                    // update progress
                    float progress = (float)(j + lz4Bufs.size()) / sourceDir.size() * 100;
                    ofSleepMillis(1);

                    // check all done
                    for (int l = 0; l < flags.size(); l++) {
                        if (!flags[l]) {
                            state = false;
                            break;
                        }
                        state = true;
                    }

                    // print flags
                    // ofLogNotice("ofApp") << "Flags: " << flags.size();
                    // for (int l = 0; l < flags.size(); l++) {
                    //     ofLogNotice("ofApp") << "Flag: " << flags[l];
                    // }
                }

                ofLogNotice("ofApp") << "All done";

                // if error, abort()
                for (int k = 0; k < error_flags.size(); k++) {
                    if (error_flags[k]) {
                        ofLogError("ofApp") << "Error: " << queue[k];
                        abort();
                    }
                }

                // write lz4 compressed frame
                for (int k = 0; k < lz4Bufs.size(); k++) {
                    uint64_t address = fp.tellp();

                    // save address
                    address_and_sizes_[k].first = address;

                    // write lz4
                    fp.write(lz4Bufs[k].getData(), lz4Bufs[k].size());
                }

                // save address and sizes
                for (int k = 0; k < address_and_sizes_.size(); k++) {
                    address_and_sizes.push_back(address_and_sizes_[k]);
                }

                float progress = (float)(j + 1) / sourceDir.size() * 100;
                progressMap[sourceDirPath] = progress;

                // clear memory
                for (int k = 0; k < lz4Bufs.size(); k++) {
                    lz4Bufs[k].clear();
                }
                for (int k = 0; k < sourcePixelMap.size(); k++) {
                    sourcePixelMap[k].clear();
                }
                lz4Bufs.clear();
                sourcePixelMap.clear();

                j += flags.size() - 1;
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
