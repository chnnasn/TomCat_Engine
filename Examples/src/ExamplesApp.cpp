#include <TomCat.h>

class Examples :public TomCat::Application
{
public:
	Examples(){}
	~Examples() {}
};
void main() {

	Examples* examples = new Examples();

	examples->Run();

	delete examples;
}