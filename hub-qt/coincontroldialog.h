/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
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

#ifndef FLOWEE_QT_COINCONTROLDIALOG_H
#define FLOWEE_QT_COINCONTROLDIALOG_H

#include "amount.h"

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

class PlatformStyle;
class WalletModel;

class CCoinControl;
class CTxMemPool;

namespace Ui {
    class CoinControlDialog;
}

#define ASYMP_UTF8 "\xE2\x89\x88"

class CoinControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CoinControlDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~CoinControlDialog();

    void setModel(WalletModel *model);

    // static because also called from sendcoinsdialog
    static void updateLabels(WalletModel*, QDialog*);
    static QString getPriorityLabel(double dPriority, double mempoolEstimatePriority);

    static QList<int64_t> payAmounts;
    static CCoinControl *coinControl;
    static bool fSubtractFeeFromAmount;

private:
    Ui::CoinControlDialog *ui;
    WalletModel *model;
    int sortColumn;
    Qt::SortOrder sortOrder;

    QMenu *contextMenu;
    QTreeWidgetItem *contextMenuItem;
    QAction *copyTransactionHashAction;
    QAction *lockAction;
    QAction *unlockAction;

    const PlatformStyle *platformStyle;

    QString strPad(QString, int, QString);
    void sortView(int, Qt::SortOrder);
    void updateView();

    enum
    {
        COLUMN_CHECKBOX,
        COLUMN_AMOUNT,
        COLUMN_LABEL,
        COLUMN_ADDRESS,
        COLUMN_DATE,
        COLUMN_CONFIRMATIONS,
        COLUMN_PRIORITY,
        COLUMN_TXHASH,
        COLUMN_VOUT_INDEX,
        COLUMN_AMOUNT_INT64,
        COLUMN_PRIORITY_INT64,
        COLUMN_DATE_INT64
    };

    // some columns have a hidden column containing the value used for sorting
    int getMappedColumn(int column, bool fVisibleColumn = true)
    {
        if (fVisibleColumn)
        {
            if (column == COLUMN_AMOUNT_INT64)
                return COLUMN_AMOUNT;
            else if (column == COLUMN_PRIORITY_INT64)
                return COLUMN_PRIORITY;
            else if (column == COLUMN_DATE_INT64)
                return COLUMN_DATE;
        }
        else
        {
            if (column == COLUMN_AMOUNT)
                return COLUMN_AMOUNT_INT64;
            else if (column == COLUMN_PRIORITY)
                return COLUMN_PRIORITY_INT64;
            else if (column == COLUMN_DATE)
                return COLUMN_DATE_INT64;
        }

        return column;
    }

private Q_SLOTS:
    void showMenu(const QPoint &);
    void copyAmount();
    void copyLabel();
    void copyAddress();
    void copyTransactionHash();
    void lockCoin();
    void unlockCoin();
    void clipboardQuantity();
    void clipboardAmount();
    void clipboardFee();
    void clipboardAfterFee();
    void clipboardBytes();
    void clipboardPriority();
    void clipboardLowOutput();
    void clipboardChange();
    void radioTreeMode(bool);
    void radioListMode(bool);
    void viewItemChanged(QTreeWidgetItem*, int);
    void headerSectionClicked(int);
    void buttonBoxClicked(QAbstractButton*);
    void buttonSelectAllClicked();
    void updateLabelLocked();
};

#endif
