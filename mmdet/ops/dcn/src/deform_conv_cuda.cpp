// modify from https://github.com/chengdazhi/Deformable-Convolution-V2-PyTorch/blob/mmdetection/mmdet/ops/dcn/src/deform_conv_cuda.c

#include <torch/torch.h>

#include <cmath>
#include <vector>

void deformable_im2col(const at::Tensor data_im,
                       const at::Tensor data_offset, const int channels,
                       const int height, const int width, const int ksize_h,
                       const int ksize_w, const int pad_h, const int pad_w,
                       const int stride_h, const int stride_w,
                       const int dilation_h, const int dilation_w,
                       const int parallel_imgs,
                       const int deformable_group, at::Tensor data_col);

void deformable_col2im(const at::Tensor data_col,
                       const at::Tensor data_offset, const int channels,
                       const int height, const int width, const int ksize_h,
                       const int ksize_w, const int pad_h, const int pad_w,
                       const int stride_h, const int stride_w,
                       const int dilation_h, const int dilation_w,
                       const int parallel_imgs,
                       const int deformable_group, at::Tensor grad_im);

void deformable_col2im_coord(const at::Tensor data_col,
                             const at::Tensor data_im, const at::Tensor data_offset,
                             const int channels, const int height,
                             const int width, const int ksize_h,
                             const int ksize_w, const int pad_h,
                             const int pad_w, const int stride_h,
                             const int stride_w, const int dilation_h,
                             const int dilation_w, const int parallel_imgs,
                             const int deformable_group, at::Tensor grad_offset);

void modulated_deformable_im2col_cuda(const at::Tensor data_im, const at::Tensor data_offset,
                                      const at::Tensor data_mask, const int batch_size, const int channels,
                                      const int height_im, const int width_im, const int height_col,
                                      const int width_col, const int kernel_h, const int kenerl_w,
                                      const int pad_h, const int pad_w, const int stride_h, const int stride_w,
                                      const int dilation_h, const int dilation_w,
                                      const int deformable_group, at::Tensor data_col);

void modulated_deformable_col2im_cuda(const at::Tensor data_col, const at::Tensor data_offset,
                                      const at::Tensor data_mask, const int batch_size, const int channels,
                                      const int height_im, const int width_im, const int height_col,
                                      const int width_col, const int kernel_h, const int kenerl_w,
                                      const int pad_h, const int pad_w, const int stride_h, const int stride_w,
                                      const int dilation_h, const int dilation_w,
                                      const int deformable_group, at::Tensor grad_im);

void modulated_deformable_col2im_coord_cuda(const at::Tensor data_col, const at::Tensor data_im,
                                            const at::Tensor data_offset, const at::Tensor data_mask,
                                            const int batch_size, const int channels, const int height_im,
                                            const int width_im, const int height_col, const int width_col,
                                            const int kernel_h, const int kenerl_w, const int pad_h,
                                            const int pad_w, const int stride_h, const int stride_w,
                                            const int dilation_h, const int dilation_w,
                                            const int deformable_group, at::Tensor grad_offset,
                                            at::Tensor grad_mask);

void shape_check(at::Tensor input, at::Tensor offset,
                 at::Tensor *gradOutput, at::Tensor weight, int kH, int kW,
                 int dH, int dW, int padH, int padW, int dilationH,
                 int dilationW, int deformable_group)
{

    AT_CHECK(weight.ndimension() == 4,
             "4D weight tensor (nOutputPlane,nInputPlane,kH,kW) expected, "
             "but got: %s",
             weight.ndimension());

    AT_CHECK(weight.is_contiguous(),
             "weight tensor has to be contiguous");

    AT_CHECK(kW > 0 && kH > 0,
             "kernel size should be greater than zero, but got kH: %d kW: %d",
             kH, kW);

    AT_CHECK((weight.size(2) == kH &&
              weight.size(3) == kW),
             "kernel size should be consistent with weight, ",
             "but got kH: %d kW: %d weight.size(2): %d, weight.size(3): %d", kH,
             kW, weight.size(2), weight.size(3));

    AT_CHECK(dW > 0 && dH > 0,
             "stride should be greater than zero, but got dH: %d dW: %d", dH, dW);

    AT_CHECK(dilationW > 0 && dilationH > 0,
             "dilation should be greater than 0, but got dilationH: %d dilationW: %d",
             dilationH, dilationW);

    int ndim = input.ndimension();
    int dimf = 0;
    int dimh = 1;
    int dimw = 2;

    if (ndim == 4)
    {
        dimf++;
        dimh++;
        dimw++;
    }

    AT_CHECK(ndim == 3 || ndim == 4,
             "3D or 4D input tensor expected but got: %s", ndim);

    long nInputPlane = weight.size(1);
    long inputHeight = input.size(dimh);
    long inputWidth = input.size(dimw);
    long nOutputPlane = weight.size(0);
    long outputHeight = (inputHeight + 2 * padH - (dilationH * (kH - 1) + 1)) / dH + 1;
    long outputWidth = (inputWidth + 2 * padW - (dilationW * (kW - 1) + 1)) / dW + 1;

    AT_CHECK(nInputPlane % deformable_group == 0,
             "input channels must divide deformable group size");

    if (outputWidth < 1 || outputHeight < 1)
        AT_ERROR(
            "Given input size: (%ld x %ld x %ld). "
            "Calculated output size: (%ld x %ld x %ld). Output size is too small",
            nInputPlane, inputHeight, inputWidth, nOutputPlane, outputHeight,
            outputWidth);

    AT_CHECK(input.size(1) == nInputPlane,
             "invalid number of input planes, expected: %d, but got: %d",
             nInputPlane, input.size(1));

    AT_CHECK((inputHeight >= kH && inputWidth >= kW),
             "input image is smaller than kernel");

    AT_CHECK(
        (offset.size(2) == outputHeight && offset.size(3) == outputWidth),
        "invalid spatial size of offset, expected height: %d width: %d, but got height: %d width: %d",
        outputHeight, outputWidth, offset.size(2), offset.size(3));

    AT_CHECK((offset.size(1) == deformable_group * 2 * kH * kW),
             "invalid number of channels of offset");

    if (gradOutput != NULL)
    {
        AT_CHECK(gradOutput->size(dimf) == nOutputPlane,
                 "invalid number of gradOutput planes, expected: %d, but got: %d",
                 nOutputPlane, gradOutput->size(dimf));

        AT_CHECK((gradOutput->size(dimh) == outputHeight &&
                  gradOutput->size(dimw) == outputWidth),
                 "invalid size of gradOutput, expected height: %d width: %d , but got height: %d width: %d",
                 outputHeight, outputWidth, gradOutput->size(dimh), gradOutput->size(dimw));
    }
}

int deform_conv_forward_cuda(at::Tensor input, at::Tensor weight,
                             at::Tensor offset, at::Tensor output,
                             at::Tensor columns, at::Tensor ones, int kW,
                             int kH, int dW, int dH, int padW, int padH,
                             int dilationW, int dilationH,
                             int deformable_group, int im2col_step)
{

    // todo: resize columns to include im2col: done
    // todo: add im2col_step as input
    // todo: add new output buffer and transpose it to output (or directly transpose output)
    // todo: possibly change data indexing because of parallel_imgs

    shape_check(input, offset, NULL, weight, kH, kW, dH, dW, padH, padW,
                dilationH, dilationW, deformable_group);

    input = input.contiguous();
    offset = offset.contiguous();
    weight = weight.contiguous();

    int batch = 1;
    if (input.ndimension() == 3)
    {
        // Force batch
        batch = 0;
        input.unsqueeze_(0);
        offset.unsqueeze_(0);
    }

    // todo: assert batchsize dividable by im2col_step

    long batchSize = input.size(0);
    long nInputPlane = input.size(1);
    long inputHeight = input.size(2);
    long inputWidth = input.size(3);

    long nOutputPlane = weight.size(0);

    long outputWidth = (inputWidth + 2 * padW - (dilationW * (kW - 1) + 1)) / dW + 1;
    long outputHeight = (inputHeight + 2 * padH - (dilationH * (kH - 1) + 1)) / dH + 1;

    AT_CHECK((offset.size(0) == batchSize), "invalid batch size of offset");

    output = output.view({batchSize / im2col_step, im2col_step, nOutputPlane, outputHeight, outputWidth});
    columns = at::zeros({nInputPlane * kW * kH, im2col_step * outputHeight * outputWidth}, input.type());

    if (ones.ndimension() != 2 || ones.size(0) * ones.size(1) < outputHeight * outputWidth)
    {
        ones = at::ones({outputHeight, outputWidth}, input.type());
    }

    input = input.view({batchSize / im2col_step, im2col_step, nInputPlane, inputHeight, inputWidth});
    offset = offset.view({batchSize / im2col_step, im2col_step,
                          deformable_group * 2 * kH * kW, outputHeight, outputWidth});

    at::Tensor output_buffer = at::zeros({batchSize / im2col_step, nOutputPlane, im2col_step * outputHeight, outputWidth}, output.type());

    for (int elt = 0; elt < batchSize / im2col_step; elt++)
    {
        deformable_im2col(
            input[elt], offset[elt], nInputPlane, inputHeight,
            inputWidth, kH, kW, padH, padW, dH, dW, dilationH, dilationW,
            im2col_step, deformable_group, columns);

        output_buffer[elt] =
            output_buffer[elt].flatten(1).addmm_(weight.flatten(1), columns).view_as(output_buffer[elt]);
    }

    output_buffer = output_buffer.view(
        {batchSize / im2col_step, nOutputPlane, im2col_step, outputHeight, outputWidth});
    output_buffer.transpose_(1, 2);
    output.copy_(output_buffer);
    output = output.view({batchSize, nOutputPlane, outputHeight, outputWidth});

    input = input.view({batchSize, nInputPlane, inputHeight, inputWidth});
    offset = offset.view({batchSize, deformable_group * 2 * kH * kW, outputHeight, outputWidth});

    if (batch == 0)
    {
        output = output.view({nOutputPlane, outputHeight, outputWidth});
        input = input.view({nInputPlane, inputHeight, inputWidth});
        offset = offset.view({offset.size(1), offset.size(2), offset.size(3)});
    }

    return 1;
}

int deform_conv_backward_input_cuda(
    at::Tensor input, at::Tensor offset, at::Tensor gradOutput,
    at::Tensor gradInput, at::Tensor gradOffset, at::Tensor weight,
    at::Tensor columns, int kW, int kH, int dW, int dH, int padW, int padH,
    int dilationW, int dilationH, int deformable_group, int im2col_step)
{

    shape_check(input, offset, &gradOutput, weight, kH, kW, dH, dW, padH,
                padW, dilationH, dilationW, deformable_group);

    input = input.contiguous();
    offset = offset.contiguous();
    gradOutput = gradOutput.contiguous();
    weight = weight.contiguous();

    int batch = 1;

    if (input.ndimension() == 3)
    {
        // Force batch
        batch = 0;
        input = input.view({1, input.size(0), input.size(1), input.size(2)});
        offset = offset.view({1, offset.size(0), offset.size(1), offset.size(2)});
        gradOutput = gradOutput.view({1, gradOutput.size(0), gradOutput.size(1), gradOutput.size(2)});
    }

    long batchSize = input.size(0);
    long nInputPlane = input.size(1);
    long inputHeight = input.size(2);
    long inputWidth = input.size(3);

    long nOutputPlane = weight.size(0);

    long outputWidth = (inputWidth + 2 * padW - (dilationW * (kW - 1) + 1)) / dW + 1;
    long outputHeight = (inputHeight + 2 * padH - (dilationH * (kH - 1) + 1)) / dH + 1;

    AT_CHECK((offset.size(0) == batchSize), 3, "invalid batch size of offset");
    gradInput = gradInput.view({batchSize, nInputPlane, inputHeight, inputWidth});
    columns = at::zeros({nInputPlane * kW * kH, im2col_step * outputHeight * outputWidth}, input.type());

    // change order of grad output
    gradOutput = gradOutput.view(
        {batchSize / im2col_step, im2col_step, nOutputPlane, outputHeight, outputWidth});
    gradOutput.transpose_(1, 2);

    gradInput = gradInput.view(
        {batchSize / im2col_step, im2col_step, nInputPlane, inputHeight, inputWidth});
    input = input.view({batchSize / im2col_step, im2col_step, nInputPlane, inputHeight, inputWidth});
    gradOffset = gradOffset.view({batchSize / im2col_step, im2col_step,
                                  deformable_group * 2 * kH * kW, outputHeight, outputWidth});
    offset = offset.view({batchSize / im2col_step, im2col_step,
                          deformable_group * 2 * kH * kW, outputHeight, outputWidth});

    for (int elt = 0; elt < batchSize / im2col_step; elt++)
    {
        columns = columns.addmm_(weight.flatten(1).transpose(0, 1), gradOutput[elt].flatten(1), 0.0f, 1.0f);

        deformable_col2im_coord(
            columns, input[elt], offset[elt],
            nInputPlane, inputHeight, inputWidth, kH, kW, padH, padW, dH, dW,
            dilationH, dilationW, im2col_step, deformable_group, gradOffset[elt]);

        deformable_col2im(
            columns, offset[elt], nInputPlane, inputHeight,
            inputWidth, kH, kW, padH, padW, dH, dW, dilationH, dilationW, im2col_step,
            deformable_group, gradInput[elt]);
    }

    gradOutput.transpose_(1, 2);
    gradOutput = gradOutput.view({batchSize, nOutputPlane, outputHeight, outputWidth});

    gradInput = gradInput.view({batchSize, nInputPlane, inputHeight, inputWidth});
    input = input.view({batchSize, nInputPlane, inputHeight, inputWidth});
    gradOffset = gradOffset.view({batchSize, deformable_group * 2 * kH * kW, outputHeight, outputWidth});
    offset = offset.view({batchSize, deformable_group * 2 * kH * kW, outputHeight, outputWidth});

    if (batch == 0)
    {
        gradOutput = gradOutput.view({nOutputPlane, outputHeight, outputWidth});
        input = input.view({nInputPlane, inputHeight, inputWidth});
        gradInput = gradInput.view({nInputPlane, inputHeight, inputWidth});
        offset = offset.view({offset.size(1), offset.size(2), offset.size(3)});
        gradOffset = gradOffset.view({offset.size(1), offset.size(2), offset.size(3)});
    }

    return 1;
}

int deform_conv_backward_parameters_cuda(
    at::Tensor input, at::Tensor offset, at::Tensor gradOutput,
    at::Tensor gradWeight, // at::Tensor gradBias,
    at::Tensor columns, at::Tensor ones, int kW, int kH, int dW, int dH,
    int padW, int padH, int dilationW, int dilationH, int deformable_group,
    float scale, int im2col_step)
{

    // todo: transpose and reshape outGrad
    // todo: reshape columns
    // todo: add im2col_step as input

    shape_check(input, offset, &gradOutput, gradWeight, kH, kW, dH, dW,
                padH, padW, dilationH, dilationW, deformable_group);

    input = input.contiguous();
    offset = offset.contiguous();
    gradOutput = gradOutput.contiguous();

    int batch = 1;

    if (input.ndimension() == 3)
    {
        // Force batch
        batch = 0;
        input = input.view(at::IntList({1, input.size(0), input.size(1), input.size(2)}));
        gradOutput = gradOutput.view({1, gradOutput.size(0),
                                      gradOutput.size(1), gradOutput.size(2)});
    }

    long batchSize = input.size(0);
    long nInputPlane = input.size(1);
    long inputHeight = input.size(2);
    long inputWidth = input.size(3);

    long nOutputPlane = gradWeight.size(0);

    long outputWidth = (inputWidth + 2 * padW - (dilationW * (kW - 1) + 1)) / dW + 1;
    long outputHeight = (inputHeight + 2 * padH - (dilationH * (kH - 1) + 1)) / dH + 1;

    AT_CHECK((offset.size(0) == batchSize), "invalid batch size of offset");

    columns = at::zeros({nInputPlane * kW * kH, im2col_step * outputHeight * outputWidth}, input.type());

    gradOutput = gradOutput.view(
        {batchSize / im2col_step, im2col_step, nOutputPlane, outputHeight, outputWidth});
    gradOutput.transpose_(1, 2);

    at::Tensor gradOutputBuffer = at::zeros_like(gradOutput);
    gradOutputBuffer = gradOutputBuffer.view(
        {batchSize / im2col_step, nOutputPlane, im2col_step, outputHeight, outputWidth});
    gradOutputBuffer.copy_(gradOutput);
    gradOutputBuffer = gradOutputBuffer.view(
        {batchSize / im2col_step, nOutputPlane, im2col_step * outputHeight, outputWidth});

    gradOutput.transpose_(1, 2);
    gradOutput = gradOutput.view({batchSize, nOutputPlane, outputHeight, outputWidth});

    input = input.view({batchSize / im2col_step, im2col_step, nInputPlane, inputHeight, inputWidth});
    offset = offset.view({batchSize / im2col_step, im2col_step,
                          deformable_group * 2 * kH * kW,
                          outputHeight, outputWidth});

    for (int elt = 0; elt < batchSize / im2col_step; elt++)
    {
        deformable_im2col(
            input[elt], offset[elt], nInputPlane, inputHeight,
            inputWidth, kH, kW, padH, padW, dH, dW, dilationH, dilationW,
            im2col_step, deformable_group, columns);

        gradWeight = gradWeight.flatten(1).addmm_(
                                              gradOutputBuffer[elt].flatten(1), columns.transpose(1, 0), 1.0, scale)
                         .view_as(gradWeight);
    }

    input = input.view({batchSize, nInputPlane, inputHeight, inputWidth});
    offset = offset.view({batchSize, deformable_group * 2 * kH * kW,
                          outputHeight, outputWidth});

    if (batch == 0)
    {
        gradOutput = gradOutput.view({nOutputPlane, outputHeight, outputWidth});
        input = input.view({nInputPlane, inputHeight, inputWidth});
    }

    return 1;
}

void modulated_deform_conv_cuda_forward(at::Tensor input, at::Tensor weight,
                                        at::Tensor bias, at::Tensor ones,
                                        at::Tensor offset, at::Tensor mask,
                                        at::Tensor output, at::Tensor columns,
                                        int kernel_h, int kernel_w,
                                        const int stride_h, const int stride_w,
                                        const int pad_h, const int pad_w,
                                        const int dilation_h, const int dilation_w,
                                        const int deformable_group, const bool with_bias)
{
    AT_CHECK(input.is_contiguous(), "input tensor has to be contiguous");
    AT_CHECK(weight.is_contiguous(), "weight tensor has to be contiguous");

    const int batch = input.size(0);
    const int channels = input.size(1);
    const int height = input.size(2);
    const int width = input.size(3);

    const int channels_out = weight.size(0);
    const int channels_kernel = weight.size(1);
    const int kernel_h_ = weight.size(2);
    const int kernel_w_ = weight.size(3);

    if (kernel_h_ != kernel_h || kernel_w_ != kernel_w)
        AT_ERROR("Input shape and kernel shape wont match: (%d x %d vs %d x %d).",
                 kernel_h_, kernel_w, kernel_h_, kernel_w_);
    if (channels != channels_kernel)
        AT_ERROR("Input shape and kernel channels wont match: (%d vs %d).",
                 channels, channels_kernel);

    const int height_out = (height + 2 * pad_h - (dilation_h * (kernel_h - 1) + 1)) / stride_h + 1;
    const int width_out = (width + 2 * pad_w - (dilation_w * (kernel_w - 1) + 1)) / stride_w + 1;

    if (ones.ndimension() != 2 ||
        ones.size(0) * ones.size(1) < height_out * width_out)
    {
        // Resize plane and fill with ones...
        ones = at::ones({height_out, width_out}, input.type());
    }

    // resize output
    output = output.view({batch, channels_out, height_out, width_out}).zero_();
    // resize temporary columns
    columns = at::zeros({channels * kernel_h * kernel_w, 1 * height_out * width_out}, input.type());

    for (int b = 0; b < batch; b++)
    {
        modulated_deformable_im2col_cuda(input[b], offset[b], mask[b],
                                         1, channels, height, width,
                                         height_out, width_out, kernel_h, kernel_w,
                                         pad_h, pad_w, stride_h, stride_w, dilation_h, dilation_w,
                                         deformable_group, columns);

        output[b] = output[b].flatten(1).addmm_(weight.flatten(1), columns).view_as(output[b]);
    }

    if (with_bias){
        output += bias.view({1, bias.size(0), 1, 1});
    }
}

void modulated_deform_conv_cuda_backward(at::Tensor input, at::Tensor weight,
                                         at::Tensor bias, at::Tensor ones,
                                         at::Tensor offset, at::Tensor mask,
                                         at::Tensor columns,
                                         at::Tensor grad_input, at::Tensor grad_weight,
                                         at::Tensor grad_bias, at::Tensor grad_offset,
                                         at::Tensor grad_mask, at::Tensor grad_output,
                                         int kernel_h, int kernel_w,
                                         int stride_h, int stride_w,
                                         int pad_h, int pad_w,
                                         int dilation_h, int dilation_w,
                                         int deformable_group, const bool with_bias)
{
    AT_CHECK(input.is_contiguous(), "input tensor has to be contiguous");
    AT_CHECK(weight.is_contiguous(), "weight tensor has to be contiguous");

    const int batch = input.size(0);
    const int channels = input.size(1);
    const int height = input.size(2);
    const int width = input.size(3);

    const int channels_kernel = weight.size(1);
    const int kernel_h_ = weight.size(2);
    const int kernel_w_ = weight.size(3);
    if (kernel_h_ != kernel_h || kernel_w_ != kernel_w)
        AT_ERROR("Input shape and kernel shape wont match: (%d x %d vs %d x %d).",
                 kernel_h_, kernel_w, kernel_h_, kernel_w_);
    if (channels != channels_kernel)
        AT_ERROR("Input shape and kernel channels wont match: (%d vs %d).",
                 channels, channels_kernel);

    const int height_out = (height + 2 * pad_h - (dilation_h * (kernel_h - 1) + 1)) / stride_h + 1;
    const int width_out = (width + 2 * pad_w - (dilation_w * (kernel_w - 1) + 1)) / stride_w + 1;

    if (ones.ndimension() != 2 ||
        ones.size(0) * ones.size(1) < height_out * width_out)
    {
        // Resize plane and fill with ones...
        ones = at::ones({height_out, width_out}, input.type());
    }

    grad_input = grad_input.view({batch, channels, height, width});
    columns = at::zeros({channels * kernel_h * kernel_w, height_out * width_out}, input.type());

    for (int b = 0; b < batch; b++)
    {
        columns.addmm_(weight.flatten(1).transpose(0, 1), grad_output[b].flatten(1), 0.0f, 1.0f);

        // gradient w.r.t. input coordinate data
        modulated_deformable_col2im_coord_cuda(columns, input[b], offset[b], mask[b],
                                               1, channels, height, width,
                                               height_out, width_out, kernel_h, kernel_w,
                                               pad_h, pad_w, stride_h, stride_w,
                                               dilation_h, dilation_w, deformable_group,
                                               grad_offset[b], grad_mask[b]);
        // gradient w.r.t. input data
        modulated_deformable_col2im_cuda(columns, offset[b], mask[b],
                                         1, channels, height, width,
                                         height_out, width_out, kernel_h, kernel_w,
                                         pad_h, pad_w, stride_h, stride_w,
                                         dilation_h, dilation_w, deformable_group,
                                         grad_input[b]);

        // gradient w.r.t. weight, dWeight should accumulate across the batch and group
        modulated_deformable_im2col_cuda(input[b], offset[b], mask[b],
                                         1, channels, height, width,
                                         height_out, width_out, kernel_h, kernel_w,
                                         pad_h, pad_w, stride_h, stride_w,
                                         dilation_h, dilation_w, deformable_group,
                                         columns);

        grad_weight = grad_weight.flatten(1).addmm_(grad_output[b].flatten(1), columns.transpose(0, 1)).view_as(grad_weight);

        if (with_bias){
            grad_bias = grad_bias.view({-1, 1}).addmm_(grad_output[b].flatten(1), ones.view({-1, 1})).view(-1);
        }
    }
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m)
{
    m.def("deform_conv_forward_cuda", &deform_conv_forward_cuda, "deform forward (CUDA)");
    m.def("deform_conv_backward_input_cuda", &deform_conv_backward_input_cuda,
          "deform_conv_backward_input (CUDA)");
    m.def("deform_conv_backward_parameters_cuda", &deform_conv_backward_parameters_cuda,
          "deform_conv_backward_parameters (CUDA)");
    m.def("modulated_deform_conv_cuda_forward", &modulated_deform_conv_cuda_forward,
          "modulated deform conv forward (CUDA)");
    m.def("modulated_deform_conv_cuda_backward", &modulated_deform_conv_cuda_backward,
          "modulated deform conv backward (CUDA)");
}
