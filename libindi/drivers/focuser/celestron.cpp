/*
    Celestron Focuser for SCT and EDGEHD

    Copyright (C) 2019 Jasem Mutlaq (mutlaqja@ikarustech.com)
    Copyright (C) 2019 Chris Rowland

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "celestron.h"
#include "connectionplugins/connectionserial.h"
#include "indicom.h"

#include <cmath>
#include <cstring>
#include <memory>

#include <termios.h>
#include <unistd.h>

static std::unique_ptr<CelestronSCT> celestronSCT(new CelestronSCT());

void ISGetProperties(const char * dev)
{
    celestronSCT->ISGetProperties(dev);
}

void ISNewSwitch(const char * dev, const char * name, ISState * states, char * names[], int n)
{
    celestronSCT->ISNewSwitch(dev, name, states, names, n);
}

void ISNewText(const char * dev, const char * name, char * texts[], char * names[], int n)
{
    celestronSCT->ISNewText(dev, name, texts, names, n);
}

void ISNewNumber(const char * dev, const char * name, double values[], char * names[], int n)
{
    celestronSCT->ISNewNumber(dev, name, values, names, n);
}

void ISNewBLOB(const char * dev, const char * name, int sizes[], int blobsizes[], char * blobs[], char * formats[],
               char * names[], int n)
{
    INDI_UNUSED(dev);
    INDI_UNUSED(name);
    INDI_UNUSED(sizes);
    INDI_UNUSED(blobsizes);
    INDI_UNUSED(blobs);
    INDI_UNUSED(formats);
    INDI_UNUSED(names);
    INDI_UNUSED(n);
}

void ISSnoopDevice(XMLEle * root)
{
    celestronSCT->ISSnoopDevice(root);
}

CelestronSCT::CelestronSCT()
{
    // Can move in Absolute & Relative motions, can AbortFocuser motion.
    // CR variable speed and sync removed
    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_ABORT );

    communicator.source = APP;
}

bool CelestronSCT::initProperties()
{
    INDI::Focuser::initProperties();

    // Focuser backlash
    // CR this is a value, positive or negative to define the direction.  It will need to be implemented
    // in the driver.
    IUFillNumber(&BacklashN[0], "STEPS", "Steps", "%.f", -500., 500, 1., 0.);
    IUFillNumberVector(&BacklashNP, BacklashN, 1, getDeviceName(), "FOCUS_BACKLASH", "Backlash",
                       MAIN_CONTROL_TAB, IP_RW, 0, IPS_IDLE);

    // Focuser min limit
    IUFillNumber(&FocusMinPosN[0], "FOCUS_MIN_VALUE", "Steps", "%.f", 0, 40000., 1., 0.);
    IUFillNumberVector(&FocusMinPosNP, FocusMinPosN, 1, getDeviceName(), "FOCUS_MIN", "Min. Position",
                       MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    // Speed range
    // CR no need to have adjustable speed, how to remove?
    FocusSpeedN[0].min   = 1;
    FocusSpeedN[0].max   = 3;
    FocusSpeedN[0].value = 1;

    // From online screenshots, seems maximum value is 60,000 steps
    // max and min positions can be read from a calibrated focuser

    // Relative Position Range
    FocusRelPosN[0].min   = 0.;
    FocusRelPosN[0].max   = 30000.;
    FocusRelPosN[0].value = 0;
    FocusRelPosN[0].step  = 1000;

    // Absolute Postition Range
    FocusAbsPosN[0].min   = 0.;
    FocusAbsPosN[0].max   = 60000.;
    FocusAbsPosN[0].value = 0;
    FocusAbsPosN[0].step  = 1000;

    // Maximum Position Settings
    FocusMaxPosN[0].max   = 60000;
    FocusMaxPosN[0].min   = 1000;
    FocusMaxPosN[0].value = 60000;
    FocusMaxPosNP.p = IP_RO;

    // Poll every 500ms
    setDefaultPollingPeriod(500);

    // Add debugging support
    addDebugControl();

    // Set default baud rate to 19200
    serialConnection->setDefaultBaudRate(Connection::Serial::B_19200);

    // Defualt port to /dev/ttyACM0
    //serialConnection->setDefaultPort("/dev/ttyACM0");

    LOG_INFO("initProperties end");
    return true;
}

bool CelestronSCT::updateProperties()
{
    INDI::Focuser::updateProperties();

    if (isConnected())
    {
        defineNumber(&BacklashNP);

        defineNumber(&FocusMinPosNP);

        if (getStartupParameters())
            LOG_INFO("Celestron SCT focuser paramaters updated, focuser ready for use.");
        else
            LOG_WARN("Failed to retrieve some focuser parameters. Check logs.");

    }
    else
    {
        deleteProperty(BacklashNP.name);
        deleteProperty(FocusMinPosNP.name);
    }

    return true;
}

bool CelestronSCT::Handshake()
{
    if (Ack())
    {
        LOG_INFO("Celestron SCT Focuser is online. Getting focus parameters...");
        return true;
    }

    LOG_INFO("Error retreiving data from Celestron SCT, please ensure Celestron SCT controller is powered and the port is correct.");
    return false;
}

const char * CelestronSCT::getDefaultName()
{
    return "Celestron SCT";
}

bool CelestronSCT::Ack()
{
    // send simple command to focuser and check response to make sure
    // it is online and responding
    // use get firmware version command
    buffer reply;
    if (!communicator.sendCommand(PortFD, FOCUSER, GET_VER, reply))
        return false;

    if (reply.size() == 4)
    {
        LOGF_INFO("Firmware Version %i.%i.%i", reply[0], reply [1], (reply[2]<<8) + reply[3]);
    }
    else
        LOGF_INFO("Firmware Version %i.%i", reply[0], reply [1]);
    return true;
}

//bool CelestronSCT::readBacklash()
//{
    // CR Backlash has to be implemented in this driver, the backlash in the motor driver
    // is only for button moves.
    // Bascklash will need to be implemented by making the move in two parts with a state machine
    // to manage this. the final move would probably happen in the timer function.
//    buffer reply;
//    if (communicator.sendCommand(PortFD, MC_GET_POS_BACKLASH, FOC, reply) == false)
//        return false;

//    int backlash = reply[0];
//    // Now set it to property
//    BacklashN[0].value = backlash;
//    LOGF_INFO("readBacklash %i", backlash);

//    BacklashNP.s = IPS_OK;

//    return true;
//}

bool CelestronSCT::readPosition()
{
    buffer reply;
    if (!communicator.sendCommand(PortFD, FOCUSER, MC_GET_POSITION, reply))
        return false;

    int position = (reply[0] << 16) + (reply[1] << 8) + reply[2];
    LOGF_DEBUG("readPosition %i", position);
    FocusAbsPosN[0].value = position;
    FocusAbsPosNP.s = IPS_OK;
    return true;
}

//bool CelestronSCT::readSpeed()
//{
//    // Same as readBacklash
//    //FocusSpeedN[0].value = speed;
//    return true;
//}

bool CelestronSCT::isMoving()
{
    buffer reply(1);
    if (!communicator.sendCommand(PortFD, FOCUSER, MC_SLEW_DONE, reply))
        return false;
    return reply[0] != 0xFF;
}


//bool CelestronSCT::sendBacklash(uint32_t steps)
//{
//    // Ditto
//    // If there is no response required then we simply send the following:
//    //return sendCommand(cmd);

//    // this will be a value that is held in the driver

//    return true;
//}

//bool CelestronSCT::SetFocuserSpeed(int speed)
//{
//    // Ditto
//    return true;
//}

// read the focuser limits from the hardware
bool CelestronSCT::readLimits()
{
    buffer reply(8);
    if(!communicator.sendCommand(PortFD, FOCUSER, FOC_GET_HS_POSITIONS, reply))
        return false;
    int lo = (reply[0] << 24) + (reply[1] << 16) + (reply[2] << 8) + reply[3];
    int hi = (reply[4] << 24) + (reply[5] << 16) + (reply[6] << 8) + reply[7];

    FocusAbsPosN[0].max = hi;
    FocusAbsPosN[0].min = lo;
    FocusAbsPosNP.s = IPS_OK;

    FocusMaxPosN[0].value = hi;
    FocusMaxPosNP.s = IPS_OK;

    FocusMinPosN[0].value = lo;
    FocusMinPosNP.s = IPS_OK;

    LOGF_INFO("read limits hi %i lo %i", hi, lo);
    return true;
}

bool CelestronSCT::ISNewNumber(const char * dev, const char * name, double values[], char * names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        // Backlash
        if (!strcmp(name, BacklashNP.name))
        {
            // just update the number
            IUUpdateNumber(&BacklashNP, values, names, n);
            BacklashNP.s = IPS_OK;
            IDSetNumber(&BacklashNP, nullptr);
            return true;
        }
    }

    return INDI::Focuser::ISNewNumber(dev, name, values, names, n);
}

bool CelestronSCT::getStartupParameters()
{
    bool rc1 = false, rc2 = false;

    if ( (rc1 = readPosition()))
        IDSetNumber(&FocusAbsPosNP, nullptr);

//    if ( (rc2 = readBacklash()))
//        IDSetNumber(&BacklashNP, nullptr);

//    if ( (rc3 = readSpeed()))
//        IDSetNumber(&FocusSpeedNP, nullptr);

    if ( (rc2 = readLimits()))
    {
        IUUpdateMinMax(&FocusAbsPosNP);

        IDSetNumber(&FocusMaxPosNP, nullptr);
        IDSetNumber(&FocusMinPosNP, nullptr);
    }

    return (rc1 && rc2);
}

IPState CelestronSCT::MoveAbsFocuser(uint32_t targetTicks)
{
    // Send command to focuser
    // CR This will need changing to implement backlash
    // If OK and moving, return IPS_BUSY (CR don't see this, it seems to just start a new move)
    // If OK and motion already done (was very small), return IPS_OK
    // If error, return IPS_ALERT
    buffer data = {(unsigned char)((targetTicks >> 16) & 0xFF),
                   (unsigned char)((targetTicks >> 8) & 0xFF),
                   (unsigned char)(targetTicks & 0xFF)};
    LOGF_DEBUG("MoveAbs %i, %x %x %x\n", targetTicks, data[0], data[1], data[2]);
    if (!communicator.commandBlind(PortFD, FOCUSER, MC_GOTO_FAST, data))
        return IPS_ALERT;

    return IPS_BUSY;
}

IPState CelestronSCT::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    int32_t newPosition = 0;

    if (dir == FOCUS_INWARD)
        newPosition = FocusAbsPosN[0].value - ticks;
    else
        newPosition = FocusAbsPosN[0].value + ticks;

    // Clamp
    newPosition = std::max(0, std::min(static_cast<int32_t>(FocusAbsPosN[0].max), newPosition));
    return MoveAbsFocuser(newPosition);
}

void CelestronSCT::TimerHit()
{
    if (!isConnected())
    {
        SetTimer(POLLMS);
        return;
    }

    // Check position
    double lastPosition = FocusAbsPosN[0].value;
    bool rc = readPosition();
    if (rc)
    {
        // Only update if there is actual change
        if (fabs(lastPosition - FocusAbsPosN[0].value) > 1)
            IDSetNumber(&FocusAbsPosNP, nullptr);
    }

    if (FocusAbsPosNP.s == IPS_BUSY || FocusRelPosNP.s == IPS_BUSY)
    {
        // CR The backlash handling will probably have to be done here, if the move state
        // shows that a backlash move has been done then the final move needs to be started
        // and the states left at IPS_BUSY

        // There are two ways to know when focuser motion is over
        // define class variable uint32_t m_TargetPosition and set it in MoveAbsFocuser(..) function
        // then compare current value to m_TargetPosition
        // The other way is to have a function that calls a focuser specific function about motion
        if (!isMoving())
        {
            FocusAbsPosNP.s = IPS_OK;
            FocusRelPosNP.s = IPS_OK;
            IDSetNumber(&FocusAbsPosNP, nullptr);
            IDSetNumber(&FocusRelPosNP, nullptr);
            LOG_INFO("Focuser reached requested position.");
        }
    }

    SetTimer(POLLMS);
}

bool CelestronSCT::AbortFocuser()
{
    // send a command to move at rate 0
    buffer data = {0};
    return communicator.commandBlind(PortFD, FOCUSER, MC_MOVE_POS, data);
}

bool CelestronSCT::saveConfigItems(FILE * fp)
{
    Focuser::saveConfigItems(fp);

    IUSaveConfigNumber(fp, &BacklashNP);

    return true;
}


