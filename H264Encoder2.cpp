#include "H264Encoder2.h"
#include <mfapi.h>
#include <Mferror.h>
#include <Wmcodecdsp.h>
#include <codecapi.h>
#include <atlbase.h>
#include <string>

HRESULT H264Encoder2::Initialize(const int VIDEO_WIDTH, const int VIDEO_HEIGHT)
{
    CLSID* pCLSIDs = NULL;   // Pointer to an array of CLISDs. 
    UINT32 count = 0;      // Size of the array.

    CComPtr<IMFAttributes> attributes;
    CComQIPtr<IMFMediaEventGenerator> eventGen;
    CComPtr<IMFMediaType> inputType;
    CComHeapPtr<IMFActivate*> activateRaw;

    // Look for a encoder.
    MFT_REGISTER_TYPE_INFO toutinfo = { MFMediaType_Video, MFVideoFormat_H264 };

    HRESULT hr = S_OK;

    UINT32 unFlags = MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;

    hr = MFTEnum(
        MFT_CATEGORY_VIDEO_ENCODER,
        unFlags,                  // Reserved
        NULL,               // Input type to match. 
        &toutinfo,          // Output type to match.
        NULL,               // Attributes to match. (None.)
        &pCLSIDs,           // Receives a pointer to an array of CLSIDs.
        &count              // Receives the size of the array.
    );

    if (SUCCEEDED(hr))
    {
        if (count == 0)
        {
            hr = MF_E_TOPO_CODEC_NOT_FOUND;
        }
    }

    //Create the MFT decoder
    if (SUCCEEDED(hr))
    {
        hr = CoCreateInstance(pCLSIDs[0], NULL,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pMFT));
    }

    if (SUCCEEDED(hr))
    {
        if (count == 0)
        {
            hr = MF_E_TOPO_CODEC_NOT_FOUND;
        }
    }

    // Get attributes
    hr = m_pMFT->GetAttributes(&attributes);

    UINT32 nameLength = 0;
    std::wstring name;

    hr = attributes->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength);
    name.resize(nameLength + 1);
    hr = attributes->GetString(MFT_FRIENDLY_NAME_Attribute, &name[0], name.size(), &nameLength);
    name.resize(nameLength);

    hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);

    hr = m_pMFT->GetStreamIDs(1, &m_dwInputID, 1, &m_dwOutputID);
    if (hr == E_NOTIMPL)
    {
        m_dwInputID = 0;
        m_dwOutputID = 0;
        hr = S_OK;
    }

    hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);

    eventGen = m_pMFT;

    hr = MFCreateMediaType(&m_pOutputType);
    hr = m_pOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    hr = m_pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    hr = m_pOutputType->SetUINT32(MF_MT_AVG_BITRATE, 3000000);
    hr = MFSetAttributeSize(m_pOutputType, MF_MT_FRAME_SIZE, VIDEO_WIDTH, VIDEO_HEIGHT);
    hr = MFSetAttributeRatio(m_pOutputType, MF_MT_FRAME_RATE, 60, 1);
    hr = m_pOutputType->SetUINT32(MF_MT_INTERLACE_MODE, 2);
    hr = m_pOutputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = m_pMFT->SetOutputType(m_dwOutputID, m_pOutputType, 0);

    for (DWORD i = 0;; i++)
    {
        inputType = nullptr;
        hr = m_pMFT->GetInputAvailableType(m_dwInputID, i, &inputType);

        hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

        hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);

        hr = inputType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);

        hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, VIDEO_WIDTH, VIDEO_HEIGHT);

        hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1);

        hr = m_pMFT->SetInputType(m_dwInputID, inputType, 0);

        break;
    }

    return hr;

}

HRESULT H264Encoder2::ProcessInput(IMFSample* pSample)
{
    if (m_pMFT == NULL)
    {
        return MF_E_NOT_INITIALIZED;
    }

    return m_pMFT->ProcessInput(m_dwInputID, pSample, 0);
}

HRESULT H264Encoder2::ProcessOutput(IMFSample** ppSample)
{
    if (m_pMFT == NULL)
    {
        return MF_E_NOT_INITIALIZED;
    }

    *ppSample = NULL;

    IMFMediaBuffer* pBufferOut = NULL;
    IMFSample* pSampleOut = NULL;

    DWORD dwStatus;

    MFT_OUTPUT_STREAM_INFO mftStreamInfo = { 0 };
    MFT_OUTPUT_DATA_BUFFER mftOutputData = { 0 };

    HRESULT hr = m_pMFT->GetOutputStreamInfo(m_dwOutputID, &mftStreamInfo);
    if (FAILED(hr))
    {
        goto done;
    }

    //create a buffer for the output sample
    hr = MFCreateMemoryBuffer(mftStreamInfo.cbSize, &pBufferOut);
    if (FAILED(hr))
    {
        goto done;
    }

    //Create the output sample
    hr = MFCreateSample(&pSampleOut);
    if (FAILED(hr))
    {
        goto done;
    }

    //Add the output buffer 
    hr = pSampleOut->AddBuffer(pBufferOut);
    if (FAILED(hr))
    {
        goto done;
    }

    //Set the output sample
    mftOutputData.pSample = pSampleOut;

    //Set the output id
    mftOutputData.dwStreamID = m_dwOutputID;

    //Generate the output sample
    hr = m_pMFT->ProcessOutput(0, 1, &mftOutputData, &dwStatus);
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
    {
        hr = S_OK;
        goto done;
    }

    // TODO: Handle MF_E_TRANSFORM_STREAM_CHANGE

    if (FAILED(hr))
    {
        goto done;
    }

    *ppSample = pSampleOut;
    (*ppSample)->AddRef();

done:
    SafeRelease(&pBufferOut);
    SafeRelease(&pSampleOut);
    return hr;
};

HRESULT H264Encoder2::Flush()
{
    if (m_pMFT == NULL)
    {
        return MF_E_NOT_INITIALIZED;
    }

    return m_pMFT->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, m_dwInputID);
}
