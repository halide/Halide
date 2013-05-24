#ifndef MAIN_WIDGET_H
#define MAIN_WIDGET_H

#include <QWidget>

class SimpleTriangleWidget;

class MainWidget : public QWidget
{
Q_OBJECT

public:
     
	MainWidget( QWidget* parent = 0 );

	void setTriangleWidget( SimpleTriangleWidget* w );

private:

	SimpleTriangleWidget* m_pSTW;

private slots:

	void handleButtonPushed();

};

#endif // MAIN_WIDGET_H
