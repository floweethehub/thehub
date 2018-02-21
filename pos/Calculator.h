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
#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <QObject>

class Calculator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentValue READ currentValue NOTIFY currentValueChanged)
    Q_PROPERTY(QString totalValue READ totalValue NOTIFY totalValueChanged)
    Q_PROPERTY(QStringList historic READ historicValues NOTIFY historicValuesChanged)
    Q_PROPERTY(bool hasTotalValue READ hasTotalValue NOTIFY totalValueChanged)
    Q_PROPERTY(int priceInCents READ priceInCents)
public:
    explicit Calculator(QObject *parent = nullptr);

    Q_INVOKABLE void addCharacter(const QString &character);
    Q_INVOKABLE void addToTotal();
    Q_INVOKABLE void startMultiplication();
    Q_INVOKABLE void subtotalButtonPresssed();
    Q_INVOKABLE void backspace();
    Q_INVOKABLE void clearAll();

    QString currentValue() const;
    QString totalValue() const;
    bool hasTotalValue() const;

    QStringList historicValues() const;

    int priceInCents() const;

signals:
    void currentValueChanged();
    void historicValuesChanged();
    void totalValueChanged();

private:
    int currentValueInt() const;
    void addCurrentToHistory();
    void calc();

    QString m_amountOfCent;
    int m_amountOfUnit = 0;
    bool m_afterDecimal = false; // user typed the dot or comma, we are not editing the cents part
    bool m_finalState = false; // user just pressed '='
    bool m_inMultiplication = false;
    int m_multiplier = 0;
    int m_totalValue = 0; // in cents
    QStringList m_history;
    QString m_currencySeparator; // TODO make configurable
};

#endif
