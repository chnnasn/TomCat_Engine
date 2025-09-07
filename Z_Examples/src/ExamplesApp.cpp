#include <TomCat.h>

class Examples :public TomCat::Application
{
public:
	Examples(){}
	~Examples() {}
};
TomCat::Application* TomCat::CreateApplication() {

	return new Examples();
}