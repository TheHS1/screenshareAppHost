#pragma once
#include <wtypes.h>
#include <mfidl.h>
#include <mfreadwrite.h>

enum EncodeMode
{
    EncodeMode_CBR,
    EncodeMode_VBR_Quality,
    EncodeMode_VBR_Peak,
    EncodeMode_VBR_Unconstrained,
};

struct LeakyBucket
{
    DWORD dwBitrate;
    DWORD msBufferSize;
    DWORD msInitialBufferFullness;
};

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

class H264Encoder2
{
public:

    H264Encoder2()
        : m_pMFT(NULL), m_dwInputID(0), m_dwOutputID(0), m_pOutputType(NULL)
    {
    }

    ~H264Encoder2()
    {
        SafeRelease(&m_pMFT);
        SafeRelease(&m_pOutputType);
    }

    HRESULT Initialize(const int, const int);
    HRESULT ProcessInput(IMFSample* pSample);
    HRESULT ProcessOutput(IMFSample** ppSample);

protected:
    DWORD           m_dwInputID;     // Input stream ID.
    DWORD           m_dwOutputID;    // Output stream ID.

    IMFTransform* m_pMFT;         // Pointer to the encoder MFT.
    IMFMediaType* m_pOutputType;  // Output media type of the encoder.

};