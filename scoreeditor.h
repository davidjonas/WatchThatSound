#ifndef SCOREEDITOR_H
#define SCOREEDITOR_H

#include "timelinewidget.h"
#include "scoresymbol.h"

class ScoreEditor : public TimeLineWidget
{
    Q_OBJECT
public:
    explicit ScoreEditor(QWidget *parent = 0);

    void drawBackground(QPainter *painter, const QRectF &rect);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void resizeEvent(QResizeEvent *event);

    void saveData(QXmlStreamWriter& xml);
    void loadData(QXmlStreamReader& xml);

signals:

public slots:
    void passInk() { m_newSymbol->ink(); }

protected:
    void selectPetal(QGraphicsItem * petal);
    void initNewSymbol();

    enum {
        PetalIndex = 100
    };

    unsigned m_gridStep;
    ScoreSymbol * m_newSymbol;
    QGraphicsItemGroup * m_colorWheel;
    QGraphicsEllipseItem * m_colorSelCircle;
    QList<ScoreSymbol *> m_symbols;

    static const int s_wheelColorCount;
    static const float s_wheelInnerRadius;
    static const float s_wheelOuterRadius;
};

#endif // SCOREEDITOR_H