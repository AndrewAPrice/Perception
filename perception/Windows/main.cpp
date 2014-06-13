#include <Windows.h>
#include "../perception.h"

int main() {
	return 0;
}

int CALLBACK WinMain(
  _In_  HINSTANCE hInstance,
  _In_  HINSTANCE hPrevInstance,
  _In_  LPSTR lpCmdLine,
  _In_  int nCmdShow
) {
	{
	// change to the parent working directory
		char path[MAX_PATH];
		_fullpath(path, "..", MAX_PATH);
		SetCurrentDirectory(path);
	}
	kmain(); /* call the kernel */
	return 0;
}