/*
 * ParallelController.cpp
 *
 *  Created on: Jul 7, 2015
 *      Author: rdegelo
 */

#include "ParallelController.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <set>

ParallelController::ParallelController(int hmin, int hmax) {
	this->hmin = hmin;
	this->hmax = hmax;

	chunks.push_back(std::make_pair(hmin, hmax));
}

ParallelController::~ParallelController() {

}

int ParallelController::getNextStep() {
	int largestChunk = -1;
	std::pair<int, int> largestChunkPair;

	for(auto &c : chunks) {
	    if(c.second -c.first > largestChunk) {
	    	largestChunkPair = c;
	    	largestChunk = c.second -c.first;
	    }
	}

	chunks.erase(std::remove(chunks.begin(), chunks.end(), largestChunkPair), chunks.end());

	int median = largestChunkPair.first + floor((largestChunkPair.second - largestChunkPair.first) / 2.0);
	if(median > 0) {
		if(largestChunkPair.second - largestChunkPair.first > 1)
			chunks.push_back(std::make_pair(largestChunkPair.first, median - 1));

		if(largestChunkPair.second != largestChunkPair.first)
			chunks.push_back(std::make_pair(median + 1, largestChunkPair.second));
	}

	return median;
}

bool ParallelController::isStepNeeded(int threadNum) {
	//std::cout << "isStepNeededRequested" << std::endl;

	return threadNum >= hmin && threadNum <= hmax;
}

void ParallelController::setStepResult(int threadNum, bool result) {
	if(isStepNeeded(threadNum)) {
		if(result == true && hmin < threadNum) {
			hmin = threadNum;
		} else if(hmax > threadNum) {
			hmax = threadNum;
		}

		std::vector<std::pair<int, int>> toErase;

		for(auto& c : chunks) {
			if(c.first > hmax) {
				toErase.push_back(c);
			} else if(c.second < hmin) {
				toErase.push_back(c);
			} else {
				c.first = std::max(hmin, c.first);
				c.second = std::min(hmax, c.second);
			}
		}

		for(auto &c : toErase) {
			chunks.erase(std::remove(chunks.begin(), chunks.end(), c), chunks.end());
		}
	}
}
