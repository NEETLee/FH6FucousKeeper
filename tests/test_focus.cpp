/*
 * test_focus.cpp - offline tuning of the focus-ring (lime-green highlight) detector.
 *
 * Loads a saved diagnostic PNG, thresholds the bright lime-green focus ring
 * in HSV, finds the largest such region's bounding box, draws it, and saves.
 *
 * Usage: test_focus.exe <input.png> [out.png]
 */

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cstdio>

int main(int argc, char **argv) {
    const char *in = (argc > 1) ? argv[1] : "build/diag_4_cc.png";
    const char *out = (argc > 2) ? argv[2] : "build/focus_out.png";

    cv::Mat bgr = cv::imread(in, cv::IMREAD_COLOR);
    if (bgr.empty()) { printf("cannot read %s\n", in); return 1; }
    printf("loaded %s (%dx%d)\n", in, bgr.cols, bgr.rows);

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    /* Lime/chartreuse focus ring: hue ~ 35-70 (OpenCV H 0-180), high S, high V. */
    cv::Mat mask;
    cv::inRange(hsv, cv::Scalar(35, 120, 180), cv::Scalar(70, 255, 255), mask);

    /* Close gaps so the thin ring forms a solid loop, then find contours. */
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(9, 9));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    /* Pick the largest bounding box that looks like a card (big enough). */
    int minW = bgr.cols / 20, minH = bgr.rows / 20;
    cv::Rect best;
    double bestArea = 0;
    for (auto &c : contours) {
        cv::Rect r = cv::boundingRect(c);
        if (r.width < minW || r.height < minH) continue;
        double area = (double)r.width * r.height;
        if (area > bestArea) { bestArea = area; best = r; }
    }

    if (bestArea > 0) {
        printf("focus ring bbox: x=%d y=%d w=%d h=%d center=(%d,%d)\n",
               best.x, best.y, best.width, best.height,
               best.x + best.width/2, best.y + best.height/2);
        cv::rectangle(bgr, best, cv::Scalar(0, 0, 255), 3);
        cv::circle(bgr, cv::Point(best.x + best.width/2, best.y + best.height/2),
                   6, cv::Scalar(0, 0, 255), -1);
    } else {
        printf("no focus ring found (contours=%zu)\n", contours.size());
    }

    cv::imwrite(out, bgr);
    cv::imwrite("build/focus_mask.png", mask);
    printf("saved %s and build/focus_mask.png\n", out);
    return 0;
}
