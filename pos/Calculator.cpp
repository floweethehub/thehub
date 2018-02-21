/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "Calculator.h"

Calculator::Calculator(QObject *parent) : QObject(parent)
{
    m_currencySeparator = ".";
}

void Calculator::addCharacter(const QString &character)
{
    if (character.isEmpty())
        return;
    if (m_finalState) {
        m_finalState = false;
        emit currentValueChanged();
        emit totalValueChanged();
    }
    QChar x = character.at(0);
    if (x.isDigit()) {
        if (m_inMultiplication) {
            m_multiplier *= 10;
            m_multiplier += x.unicode() - '0';
        }
        else if (m_afterDecimal) {
            if (m_amountOfCent.length() >= 2)
                return;
            m_amountOfCent.append(character);
        }
        else {
            m_amountOfUnit *= 10;
            m_amountOfUnit += x.unicode() - '0';
        }
        emit currentValueChanged();
    } else if (x == "." || x == "," || x == m_currencySeparator) {
        m_afterDecimal = true;
    } else if (x == Qt::Key_Backspace) {
        backspace();
    }
}

void Calculator::addToTotal()
{
    if (m_amountOfCent.isEmpty() && m_amountOfUnit == 0)
        return;
    addCurrentToHistory();
    calc();
    emit totalValueChanged();
    emit currentValueChanged();
}

void Calculator::startMultiplication()
{
    if (m_inMultiplication || m_finalState)
        return;
    m_multiplier = 0;
    m_inMultiplication = true;
    emit currentValueChanged();
}

void Calculator::subtotalButtonPresssed()
{
    addCurrentToHistory();
    calc();
    m_finalState = true;
    // special rendering
    emit totalValueChanged();
    emit currentValueChanged();
}

void Calculator::calc()
{
    if (m_inMultiplication) {
        m_inMultiplication = false;
        m_totalValue += currentValueInt() * m_multiplier;
        m_multiplier = 0;
    } else {
        m_totalValue += currentValueInt();
    }
    m_amountOfCent.clear();
    m_amountOfUnit = 0;
    m_afterDecimal = false;
}

void Calculator::backspace()
{
    if (m_inMultiplication) {
        m_multiplier /= 10;
    } else if (m_afterDecimal) {
        if (m_amountOfCent.isEmpty())
            m_afterDecimal = false;
        else
            m_amountOfCent = m_amountOfCent.mid(0, m_amountOfCent.length() - 1);
    } else {
        m_amountOfUnit /= 10;
    }
    emit currentValueChanged();
}

void Calculator::clearAll()
{
    m_amountOfCent.clear();
    m_amountOfUnit = 0;
    m_afterDecimal = false;
    m_multiplier = 0;
    m_inMultiplication = false;
    m_history.clear();
    emit currentValueChanged();
    emit totalValueChanged();
    emit historicValuesChanged();
}

QString Calculator::currentValue() const
{
    if (m_finalState)
        return totalValue();
    int cent = m_amountOfCent.toInt();
    if (m_amountOfCent.length() == 1)
        cent *= 10;

    auto answer = QString("%1%2%3").arg(m_amountOfUnit).arg(m_currencySeparator).arg(cent, 2, 10, QChar('0'));
    if (m_inMultiplication) {
        answer += QString(" x %1").arg(m_multiplier);
    }
    return answer;
}

QString Calculator::totalValue() const
{
    return QString("%1%2%3").arg(m_totalValue / 100).arg(m_currencySeparator).arg(m_totalValue % 100, 2, 10, QChar('0'));
}

bool Calculator::hasTotalValue() const
{
    if (m_finalState)
        return false;
    if (m_history.size() == 1)
        return false;
    return m_totalValue != 0;
}

QStringList Calculator::historicValues() const
{
    return m_history;
}

int Calculator::priceInCents() const
{
    if (m_totalValue == 0) {
        int cent = m_amountOfCent.toInt();
        if (m_amountOfCent.length() == 1)
            cent *= 10;
        return m_amountOfUnit * 100 + cent;
    }
    return m_totalValue;
}

int Calculator::currentValueInt() const
{
    Q_ASSERT(!m_finalState);
    Q_ASSERT(!m_inMultiplication);
    return currentValue().replace(m_currencySeparator, "").toInt();
}

void Calculator::addCurrentToHistory()
{
    QString cur = currentValue();
    if (m_inMultiplication) {
        m_inMultiplication = false;
        int sum = currentValueInt() * m_multiplier;
        m_inMultiplication = true;
        cur = QString("(%1) = %2%3%4").arg(cur).arg(sum / 100).arg(m_currencySeparator).arg(sum %100, 2, 10, QChar('0'));
    }
    if (!m_history.isEmpty())
        cur += " +";
    m_history.insert(0, cur);
    emit historicValuesChanged();
}
