// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

//
// This directive enables TimeStamp debug message output which is used by post process analysis.
// TimeStamp debug messages go to std::cerr.
// Without this directive, all TimeStamp debug message output code is disabled at compile time,
// and they are removed from the source. We should disable this directive for the release version.
//
//#define TIMESTAMP_ENABLE

#include <string>

namespace mcrt_computation {

#ifdef TIMESTAMP_ENABLE
#define McrtTimeStamp(label, msg) TimeStampDebugMsg::out("mcrt_computation", label, msg)
#else // else TIMESTAMP_ENABLE
#define McrtTimeStamp(label, msg)
#endif // end else TIMESTAMP_ENABLE

class TimeStampDebugMsg
{
public:
    //
    // moduleName : module-name (like mcrt_computation, test_restart, moonray, ...)
    // label : execution task label name
    //         This is used for postprocess analysis of execution task name and/or block.
    //         task block start should be ended by "{" (example "onMessage RDLMessage {")
    //         task block end should be ended by "}" (example "onMessage RDLMessage }")
    //         This "{" and "}" character at the end of the label is important for
    //         post-process analysis and indentation.
    //         Without an end by "{" and "}", it indicates the middle of the task and
    //         you can label it by any string. (example "RDLMessage pass blockA")
    // msg : comment message for post-process analysis
    //
    static void out(const std::string& moduleName, const std::string& label, const std::string& msg);
};

#ifdef POST_ANALYSIS_CODE_EXAMPLE
#
# This is an example Python code to analyze TimeStamp log info
#
import sys

globalTimeStampId = 0
globalIndentLevel = 0

class SingleTimeStamp:
    def __init__(self, timeStampId, timeStampTbl, progName, timeStamp, label, msg):
        self.timeStampId = timeStampId
        self.timeStampTbl = timeStampTbl
        self.progName = progName
        self.timeStamp = timeStamp
        self.label = label
        self.msg = msg
        self.blockStartFlag = self.__isBlockStart()
        self.blockEndFlag = self.__isBlockEnd()
        self.labelName = self.__getLabelName()
        self.indentLevel = -1
        self.blockStartId = -1
        self.timeDeltaFromStart = 0
        self.timeDeltaPrevAction = 0
        self.timeDeltaBlock = 0
        self.__setIndentLevel()
        self.__setBlockStartId()
        self.__setDeltaTime()

    def show(self):
        print("SingleTimeStamp {")
        print("  timeStampId:" + str(self.timeStampId))
        print("  progName:" + self.progName)
        print("  timeStamp:" + self.timeStamp)
        print("  label:" + self.label)
        print("  msg:" + self.msg)
        print("  blockStartFlag:" + str(self.blockStartFlag))
        print("  blockEnd Flag:" + str(self.blockEndFlag))
        print("  labelName:" + self.labelName)
        print("  indentLevel:" + str(self.indentLevel)) 
        print("  blockStartId:" + str(self.blockStartId)) 
        print("  timeDeltaFromStart:" + str(self.timeDeltaFromStart)) 
        print("  timeDeltaPrevAction:" + str(self.timeDeltaPrevAction))
        print("  timeDeltaBlock:" + str(self.timeDeltaBlock))
        print("}");

    def outTitle(self):
        print(" L# duration    delta    delta          progName description")
        print("                 prev    block")
        #          01234567 01234567 01234567 01234567890123456

    def outResult(self):
        head = ""
        for id in range(1, self.indentLevel):
            head += "  "
        print("{: >3}".format(self.timeStampId) + " " +
              self.__strTimeDelta() + " " + self.progName.rjust(17) + " " +
              head + self.label + " ... " + self.msg)

    # private method ------------------------------

    def __getPrevTimeStampId(self):
        if not self.prevTimeStamp:
            return -1
        return self.prevTimeStamp.timeStampId    
    def __getLabelName(self):
        if self.blockStartFlag:
            id = self.label.find("{")
            return self.label[0:id]
        elif self.blockEndFlag:
            id = self.label.find("}")
            return self.label[0:id]
        else:
            return self.label
    def __isBlockStart(self):
        return self.label.find("{") >= 0
    def __isBlockEnd(self):
        return self.label.find("}") >= 0
    def __setIndentLevel(self):
        global globalIndentLevel
        if self.blockStartFlag:
            globalIndentLevel += 1
            self.indentLevel = globalIndentLevel
        elif self.blockEndFlag:
            self.indentLevel = globalIndentLevel
            globalIndentLevel -= 1
        else:    
            self.indentLevel = globalIndentLevel
        # print("globalIndentLevel:" + str(globalIndentLevel))
    def __setBlockStartId(self):
        # print("__setBlockStartId() myLabelName:" + self.labelName + " myTimeStampId:" + str(self.timeStampId))
        if self.blockStartFlag == False and self.blockEndFlag == True:
            for id in reversed(range(self.timeStampId)):
                # print("  id:" + str(id) + " labelName:" + self.timeStampTbl[id].labelName)
                if self.timeStampTbl[id].labelName == self.labelName:
                    self.blockStartId = id
                    return
    def __setDeltaTime(self):
        if self.timeStampId > 0:
            self.timeDeltaFromStart = int(self.timeStamp) - int(self.timeStampTbl[0].timeStamp)
            self.timeDeltaPrevAction = int(self.timeStamp) - int(self.timeStampTbl[self.timeStampId - 1].timeStamp)
        if self.blockStartId >= 0:
            self.timeDeltaBlock = int(self.timeStamp) - int(self.timeStampTbl[self.blockStartId].timeStamp)
    def __strTimeDelta(self):
        timeDeltaFromStartStr = self.__timeStr(self.timeDeltaFromStart)
        timeDeltaPrevStr = self.__timeStr(self.timeDeltaPrevAction)
        if self.blockStartId >= 0:
            timeDeltaBlockStr = self.__timeStr(self.timeDeltaBlock)
        else:
            timeDeltaBlockStr = "        "
        return timeDeltaFromStartStr + " " + timeDeltaPrevStr + " " + timeDeltaBlockStr
    def __timeStr(self, timeVal):
        return "{: >8.3f}".format(self.__timeValToSec(timeVal))
    def __timeValToSec(self, timeVal):
        return float(timeVal) * 0.000001 # return sec
        # return timeVal
    
#------------------------------------------------------------------------------------------

def getItemByKey(fields, key):
    for item in fields:
        if key in item:
            return item[item.find(key) + len(key):]
    return ""        


def genSingleTimeStamp(currLine, timeStampTbl):
    fields = currLine.split(",")
    global globalTimeStampId
    timeStampId = globalTimeStampId
    globalTimeStampId += 1
    progName = fields[1]
    timeStamp = fields[2]
    label = getItemByKey(fields, "label=")
    msg = getItemByKey(fields, "msg=")
    return SingleTimeStamp(timeStampId, timeStampTbl, progName, timeStamp, label, msg)

def getTimeStampInfo(filename):
    with open(filename, "r") as file:
        multiTimeStamp = []
        for line in file:
            if ">> timeStamp" in line:
                currLine = line.strip()
                currTimeStamp = genSingleTimeStamp(currLine, multiTimeStamp)
                multiTimeStamp.append(currTimeStamp)
        return multiTimeStamp

def showMultiTimeStampTbl(tbl):
    for item in tbl:
        item.show()

def outResult(tbl):
    tbl[0].outTitle()
    for item in tbl:
        item.outResult()

def main():
    args = sys.argv
    if len(args) != 2:
        print("Usage : " + args[0] + " <logname>")
        return
    filename = args[1]
    print("filename:", filename) 
    multiTimeStampTbl = getTimeStampInfo(filename)
    # showMultiTimeStampTbl(multiTimeStampTbl)
    outResult(multiTimeStampTbl)

if __name__ == '__main__':
    main()
#endif // end POST_ANALYSIS_CODE_EXAMPLE

} // namespace mcrt_computation
