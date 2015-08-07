/*
 * ParallelController.h
 *
 *  Created on: Jul 7, 2015
 *      Author: rdegelo
 */

#ifndef PARALLELCONTROLLER_H_
#define PARALLELCONTROLLER_H_

#include <vector>


class ParallelController {
public:
	ParallelController(int hmin, int hmax);
	virtual ~ParallelController();

	//Maior Intervalo / 2
	int getNextStep();
	void setStepResult(int threadNum, bool result);
	bool isStepNeeded(int threadNum);
private:
	int hmin;
	int hmax;
	std::vector<std::pair<int, int>> chunks;
};

#endif /* PARALLELCONTROLLER_H_ */
