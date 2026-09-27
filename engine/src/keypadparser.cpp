/*
  Q Light Controller Plus
  keypadparser.cpp

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <cmath>

#include "keypadparser.h"
#include "qlcmacros.h"
#include "universe.h"

KeyPadParser::KeyPadParser()
{
}

QList<SceneValue> KeyPadParser::parseCommand(Doc *doc, QString command,
                                             QByteArray &uniData)
{
    QList<SceneValue> values;
    if (doc == NULL || command.isEmpty())
        return values;

    QStringList tokens = command.split(" ");

    int lastCommand = CommandNone;
    quint32 fromChannel = 0;
    quint32 toChannel = 0;
    quint32 byChannel = 1;
    bool channelSet = false;
    float fromValue = 0;
    float toValue = 0;
    int thruCount = 0;

    foreach (QString token, tokens)
    {
        if (token.isEmpty())
            continue;

        if (token == "AT")
        {
            lastCommand = CommandAT;
        }
        else if (token == "THRU")
        {
            lastCommand = CommandTHRU;
        }
        else if (token == "FULL")
        {
            toValue = 255;
            lastCommand = CommandFULL;
        }
        else if (token == "ZERO")
        {
            toValue = 0;
            lastCommand = CommandZERO;
        }
        else if (token == "BY")
        {
            lastCommand = CommandBY;
        }
        else if (token == "+")
        {
            lastCommand = CommandPlus;
        }
        else if (token == "-")
        {
            lastCommand = CommandMinus;
        }
        else if (token == "+%")
        {
            lastCommand = CommandPlusPercent;
        }
        else if (token == "-%")
        {
            lastCommand = CommandMinusPercent;
        }
        else if (token == "%")
        {
            if (lastCommand == CommandPlus)
                lastCommand = CommandPlusPercent;
            else if (lastCommand == CommandMinus)
                lastCommand = CommandMinusPercent;
        }
        else
        {
            // most likely a number
            bool ok = false;
            int number = token.toUInt(&ok);

            if (ok == false)
                continue;

            switch (lastCommand)
            {
                case CommandNone:
                    // no command: this is a channel number
                    if (number <= 0)
                        break;

                    fromChannel = number;
                    toChannel = fromChannel;
                    // a channel beyond the universe data reads as 0 (it is
                    // skipped below anyway) - at() would assert
                    fromValue = number <= uniData.length() ? uchar(uniData.at(number - 1)) : 0;
                    toValue = fromValue;
                    channelSet = true;
                break;
                case CommandAT:
                    fromValue = float(number);
                    toValue = fromValue;
                break;
                case CommandTHRU:
                    if (thruCount == 0)
                        toChannel = number;
                    else
                        toValue = float(number);
                    thruCount++;
                break;
                case CommandFULL:
                    fromValue = 255;
                    toValue = 255;
                break;
                case CommandZERO:
                    fromValue = 0;
                    toValue = 0;
                break;
                case CommandBY:
                    // BY 0 (or a value wrapping negative) would never advance
                    // the channel loop below
                    if (number > 0)
                        byChannel = number;
                break;
                case CommandPlus:
                case CommandMinus:
                    toValue = number;
                break;
                case CommandPlusPercent:
                case CommandMinusPercent:
                    toValue = float(number) / 100.0;
                break;
            }
        }
    }

    /** handle the case where channel(s) are not specified.
     *  Compose a list of values based on the last channel list */

    if (channelSet == false)
    {
        if (m_channels.isEmpty())
            return values;

        for (int i = 0; i < m_channels.count(); i++)
        {
            SceneValue scv;

            scv.channel = m_channels.at(i);
            scv.value = toValue;
            values.append(scv);
        }

        return values;
    }
    else
    {
        m_channels.clear();
    }

    // A range ending before it starts selects nothing (and "THRU 0" would
    // make toChannel - 1 wrap to UINT_MAX: an endless loop below).
    if (toChannel < fromChannel)
        return values;

    float valueDelta = 0;
    if (toValue != fromValue)
    {
        valueDelta = (float(toChannel) - float(fromChannel)) / float(byChannel);
        valueDelta = (float(toValue) - float(fromValue)) / valueDelta;
    }

    // Channels past the universe end are skipped anyway: stop iterating
    // there (after valueDelta, so a fade's slope is unchanged).
    if (toChannel > UNIVERSE_SIZE)
        toChannel = UNIVERSE_SIZE;

    for (quint32 i = fromChannel - 1; i <= toChannel - 1; i += byChannel)
    {
        uchar uniValue = 0;
        SceneValue scv;

        if (i >= UNIVERSE_SIZE)
            continue;

        if (quint32(uniData.length()) > i)
            uniValue = uchar(uniData.at(i));

        scv.channel = i;
        if (lastCommand == CommandPlus)
            scv.value = CLAMP(uniValue + toValue, 0, 255);
        else if (lastCommand == CommandMinus)
            scv.value = CLAMP(uniValue - toValue, 0, 255);
        else if (lastCommand == CommandPlusPercent)
            scv.value = CLAMP(lrintf(uniValue * (1.0 + toValue)), 0, 255);
        else if (lastCommand == CommandMinusPercent)
            scv.value = CLAMP(lrintf(uniValue - (float(uniValue) * toValue)), 0, 255);
        else if (lastCommand == CommandZERO)
            scv.value = 0;
        else if (lastCommand == CommandFULL)
            scv.value = 255;
        else
            scv.value = uchar(fromValue);

        if (m_channels.contains(scv.channel) == false)
            m_channels.append(scv.channel);

        values.append(scv);
        fromValue += valueDelta;
    }

    return values;
}
