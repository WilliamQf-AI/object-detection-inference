#include "TensorFlowObjectDetection.h"


void TensorFlowObjectDetection::init(const char** classNames,
    String modelFile, 
    String configFile,     
    int frameWidth, 
    int frameHeight,        
    size_t inWidth,
    size_t inHeight,
    float inScaleFactor,
    float meanVal,
    float confidenceThreshold){
    classNames_ = classNames;
    inWidth_ = inWidth;
    inHeight_ = inHeight;
    inScaleFactor_ = inScaleFactor;
    meanVal_ = meanVal;
    confidenceThreshold_ = confidenceThreshold;		
	net_ = readNetFromTensorflow(modelFile, configFile);
	if (net_.empty())
    {
        cerr << "Can't load network by using the mode file: " << std::endl;
        cerr << modelFile << std::endl;
        exit(-1);
    }
	
}


void TensorFlowObjectDetection::run_tf(Mat& frame){
    //Mat inputBlob = blobFromImage(frame,1./255,Size(300,300));
    Mat inputBlob = blobFromImage(frame, inScaleFactor_,
                                  Size(inWidth_, inHeight_), meanVal_, false);
    
    //! [Set input blob]
	net_.setInput(inputBlob);  //set the network input
	//! [Set input blob]

	//! [Make forward pass]
	//Mat detection = net_.forward("detection_out"); //compute output
    Mat detection = net_.forward();
	//! [Make forward pass]
    std::vector<double> layersTimings;
    double freq = getTickFrequency() / 1000;
    double time = net_.getPerfProfile(layersTimings) / freq;
    cout << "Inference time, ms: " << time << endl;

    Mat detectionMat(detection.size[2], detection.size[3], CV_32F, detection.ptr<float>());

    for(int i = 0; i < detectionMat.rows; i++)
    {
        float confidence = detectionMat.at<float>(i, 2);

        if(confidence > confidenceThreshold_)
        {
            size_t objectClass = (size_t)(detectionMat.at<float>(i, 1));

            int xLeftBottom = static_cast<int>(detectionMat.at<float>(i, 3) * frame.cols);
            int yLeftBottom = static_cast<int>(detectionMat.at<float>(i, 4) * frame.rows);
            int xRightTop = static_cast<int>(detectionMat.at<float>(i, 5) * frame.cols);
            int yRightTop = static_cast<int>(detectionMat.at<float>(i, 6) * frame.rows);

            ostringstream ss;
            ss << confidence;
            String conf(ss.str());

            Rect object((int)xLeftBottom, (int)yLeftBottom,
                        (int)(xRightTop - xLeftBottom),
                        (int)(yRightTop - yLeftBottom));

            rectangle(frame, object, Scalar(0, 255, 0));
            String label = String(classNames_[objectClass]) + ": " + conf;
            int baseLine = 0;
            Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            rectangle(frame, Rect(Point(xLeftBottom, yLeftBottom - labelSize.height),
                                  Size(labelSize.width, labelSize.height + baseLine)),
                      Scalar(255, 255, 255), CV_FILLED);
            putText(frame, label, Point(xLeftBottom, yLeftBottom),
                    FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0,0,0));
        }
    }
}


