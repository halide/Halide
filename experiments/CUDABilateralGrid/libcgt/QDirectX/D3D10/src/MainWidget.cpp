#include "MainWidget.h"

#include "SimpleTriangleWidget.h"

#include <QPushButton>
#include <QHBoxLayout>

MainWidget::MainWidget( QWidget* parent )
	: QWidget( parent )
{
	m_pSTW = NULL;

	QPushButton* b = new QPushButton( "Rotate" );
	QObject::connect( b, SIGNAL( released() ), this, SLOT( handleButtonPushed() ) );

	QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget( b );
    setLayout( layout );
}

void MainWidget::setTriangleWidget( SimpleTriangleWidget* w )
{
	m_pSTW = w;
}

void MainWidget::handleButtonPushed()
{
	if( m_pSTW != NULL )
	{
		m_pSTW->m_bRotating = !( m_pSTW->m_bRotating );
	}
}
