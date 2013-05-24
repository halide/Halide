#include "SimpleTriangleWidget.h"

#include <QApplication>
#include "MainWidget.h"

int main( int argc, char* argv[] )
{
	QApplication app( argc, argv );

	SimpleTriangleWidget stw;
	stw.initialize( 800, 800 );
	stw.show();

	MainWidget w;
	w.setTriangleWidget( &stw );
	w.show();

	return app.exec();
}
