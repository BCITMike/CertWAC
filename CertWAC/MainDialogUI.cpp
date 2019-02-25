#include <map>
#include <sstream>
#include <string>
#include "ComputerCertificate.h"
#include "ErrorDialog.h"
#include "ErrorRecord.h"
#include "InstallInfo.h"
#include "MainDialog.h"
#include "StringUtility.h"

static std::wstring SplitForOutput(const std::vector<std::wstring>& SplitInput)
{
	std::wstring Output{};
	if (SplitInput.size() > 1)
	{
		for (auto const& InputLine : SplitInput)
		{
			Output.append(L"\r\n-  " + InputLine);
		}
	}
	else if (SplitInput.size() == 1)
		Output = L" " + SplitInput[0];
	Output.append(L"\r\n");
	return Output;
}

static std::wstring SplitForOutput(const std::wstring& GluePattern, const std::wstring& Composite)
{
	return SplitForOutput(SplitString(GluePattern, Composite));
}

INT_PTR CALLBACK MainDialog::SharedDialogProc(HWND hDialog, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	MainDialog* AppDialog{ nullptr };
	if (uMessage == WM_INITDIALOG)
	{
		AppDialog = (MainDialog*)lParam;
		AppDialog->HandleDialogMain = hDialog;
	}
	else
		AppDialog = (MainDialog*)GetWindowLongPtr(hDialog, GWL_USERDATA);

	if (AppDialog)
		return AppDialog->ThisDialogProc(uMessage, wParam, lParam);
	return FALSE;
}

INT_PTR CALLBACK MainDialog::ThisDialogProc(UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	switch (uMessage)
	{
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			StartActions();
			return TRUE;
		case IDCANCEL:
			SendMessage(HandleDialogMain, WM_CLOSE, 0, 0);
			return TRUE;
		case IDC_CERTLIST:
			if (HIWORD(wParam) == CBN_SELCHANGE)
				DisplayCertificate();
			break;
		case IDC_REFRESH:
			GetWACInstallationInfo();
			DisplayCertificateList();
			break;
		}
		break;
		break;
	case WM_INITDIALOG:
	{
		SetLastError(ERROR_SUCCESS);
		SetWindowLongPtr(HandleDialogMain, GWL_USERDATA, (LONG_PTR)this);
		if (auto LastError = GetLastError())
		{
			ErrorDialog(AppInstance, HandleDialogMain, ErrorRecord(LastError, L"Setting up application environment"));
			PostQuitMessage(LastError);
		}
		else
		{
			auto InitResult = InitDialog();
			SetPictureBoxImage(IDC_ICONWACINSTALLED, InitResult == ERROR_SUCCESS);
			DisplayCertificateList();
		}
	}
	break;
	case WM_CLOSE:
		DestroyWindow(HandleDialogMain);
		DeleteObject(TinyGreenBox);
		DeleteObject(TinyRedBox);
		return TRUE;
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	}
	return FALSE;
}

void MainDialog::DisplayCertificateList() noexcept
{
	EnableDialogItem(IDC_CERTLIST, false);
	size_t ItemCount{ Certificates.size() };
	HWND CertList{ GetDlgItem(HandleDialogMain, IDC_CERTLIST) };
	SendMessage(CertList, CB_RESETCONTENT, 0, 0);

	if (ItemCount)
	{
		for (auto const& Certificate : Certificates)
		{
			SendMessage(CertList, CB_ADDSTRING, 0, (LPARAM)Certificate.FQDNFromSimpleRDN(Certificate.SubjectName()).c_str());
		}
		RECT CertListComboSize{ 0 };
		GetWindowRect(CertList, &CertListComboSize);
		SetWindowPos(CertList, 0, 0, 0, CertListComboSize.right - CertListComboSize.left,
			(SendMessage(CertList, CB_GETITEMHEIGHT, 0, 0) * ItemCount * 2), SWP_NOMOVE | SWP_NOZORDER);
		EnableDialogItem(IDC_CERTLIST, true);
	}
	else
		SendMessage(CertList, CB_ADDSTRING, 0, (LPARAM)L"No computer certificates found");
	SendMessage(CertList, CB_SETCURSEL, 0, 0);
	DisplayCertificate();
}

void MainDialog::DisplayCertificate()
{
	std::wstring CertificateText{};
	bool StatusGreenWACDetection{ CmdlineModifyPath.size() > 0 };
	bool StatusGreenCertificateValid{ false };
	bool StatusGreenServerAuth{ false };
	bool StatusGreenPrivateKey{ false };

	HWND CertList{ GetDlgItem(HandleDialogMain, IDC_CERTLIST) };
	if (IsWindowEnabled(CertList) && Certificates.size())
	{
		ComputerCertificate& Certificate{ Certificates.at(SendMessage(CertList, CB_GETCURSEL, 0, 0)) };
		Thumbprint = Certificate.Thumbprint();
		std::wstringstream CertificateDisplay{};
		CertificateDisplay << L"Subject:" << SplitForOutput(L", ?", Certificate.SubjectName());
		CertificateDisplay << L"Issuer: " << SplitForOutput(L", ?", Certificate.Issuer());
		CertificateDisplay << L"Subject Alternate Names:" << SplitForOutput(Certificate.SubjectAlternateNames());
		CertificateDisplay << L"Valid from: " << Certificate.ValidFrom() << L"\r\n";
		CertificateDisplay << L"Valid to: " << Certificate.ValidTo() << L"\r\n";
		CertificateDisplay << L"Thumbprint: " << Thumbprint;
		CertificateText = CertificateDisplay.str();
		SetPictureBoxImage(IDC_ICONCERTVALID, (StatusGreenCertificateValid = Certificate.IsWithinValidityPeriod()));
		SetPictureBoxImage(IDC_ICONSERVAUTHALLOWED, (StatusGreenServerAuth = Certificate.HasServerAuthentication()));
		SetPictureBoxImage(IDC_ICONHASPRIVATEKEY, (StatusGreenPrivateKey = Certificate.HasPrivateKey()));
	}
	SetDlgItemText(HandleDialogMain, IDC_CERTDETAILS, CertificateText.c_str());
	EnableDialogItem(IDOK, StatusGreenWACDetection && StatusGreenCertificateValid &&
		StatusGreenServerAuth && StatusGreenPrivateKey);
}

void MainDialog::SetPictureBoxImage(const INT PictureBoxID, const bool Good)
{
	HBITMAP SelectedImage = Good ? TinyGreenBox : TinyRedBox;
	SendDlgItemMessage(HandleDialogMain, PictureBoxID, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)SelectedImage);
}

const DWORD MainDialog::InitDialog() noexcept
{
	HINSTANCE Instance{ GetModuleHandle(NULL) };
	AppIcon = LoadIcon(Instance, MAKEINTRESOURCE(IDI_CERTWAC));
	TinyGreenBox = LoadBitmap(Instance, MAKEINTRESOURCE(IDB_TINYGREENBOX));
	TinyRedBox = LoadBitmap(Instance, MAKEINTRESOURCE(IDB_TINYREDBOX));
	SendMessage(HandleDialogMain, WM_SETICON, ICON_SMALL, (LPARAM)AppIcon);
	SetPictureBoxImage(IDC_ICONWACINSTALLED, false);
	SetPictureBoxImage(IDC_ICONCERTVALID, false);
	SetPictureBoxImage(IDC_ICONSERVAUTHALLOWED, false);
	SetPictureBoxImage(IDC_ICONHASPRIVATEKEY, false);
	auto[CertLoadError, CertificateList] = GetComputerCertificates();
	Certificates = CertificateList;
	auto ErrorCode = GetWACInstallationInfo();
	return ErrorCode;
}

MainDialog::MainDialog(HINSTANCE Instance) : AppInstance(Instance)
{
	HandleDialogMain = CreateDialogParam(AppInstance, MAKEINTRESOURCE(IDD_MAIN), 0, &SharedDialogProc, (LPARAM)this);
}